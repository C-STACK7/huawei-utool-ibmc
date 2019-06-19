/*
* Copyright © Huawei Technologies Co., Ltd. 2018-2019. All rights reserved.
* Description: command handler for `fwupdate`
* Author:
* Create: 2019-06-14
* Notes:
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include "cJSON_Utils.h"
#include "commons.h"
#include "curl/curl.h"
#include "zf_log.h"
#include "constants.h"
#include "command-helps.h"
#include "command-interfaces.h"
#include "argparse.h"
#include "redfish.h"
#include "string_utils.h"
#include "url_parser.h"

#define LOG_HEAD "{\"log\":[   \n"
#define LOG_TAIL "\n]}"
#define MAX_LOG_ENTRY_LEN 512
#define LOG_ENTRY_FORMAT "\t{\n\t\t\"Time\": \"%s\",\n\t\t\"Stage\": \"%s\",\n\t\t\"State\": \"%s\",\n\t\t\"Note\": \"%s\"\n\t}, \n"

#define STAGE_UPDATE "Update firmware"
#define STAGE_UPLOAD_FILE "Upload File"
#define STAGE_DOWNLOAD_FILE "Download File"

#define PROGRESS_START "Start"
#define PROGRESS_RUN "In Progress"
#define PROGRESS_FINISHED "Finish"
#define PROGRESS_SUCCESS "Success"
#define PROGRESS_FAILED "Failed"
#define PROGRESS_INVAILD_URI "Invalid URI"

typedef struct _UpdateFirmwareOption
{
    char *imageURI;
    char *activateMode;
    char *firmwareType;

    char *psn;
    bool isLocalFile;
    FILE *logFileFP;
    time_t startTime;
} UtoolUpdateFirmwareOption;


static const char *MODE_CHOICES[] = {UPGRADE_ACTIVATE_MODE_AUTO, UPGRADE_ACTIVATE_MODE_MANUAL, NULL};
static const char *TYPE_CHOICES[] = {"BMC", "BIOS", "CPLD", "PSUFW", NULL};
static const char *IMAGE_PROTOCOL_CHOICES[] = {"HTTPS", "SCP", "SFTP", "CIFS", "TFTP", "NFS", NULL};

static const char *OPTION_IMAGE_URI_REQUIRED = "Error: option `image-uri` is required.";
static const char *OPTION_IMAGE_URI_ILLEGAL = "Error: option `image-uri` is illegal.";
static const char *OPTION_IMAGE_URI_NO_SCHEMA = "Error: URI is not a local file nor a remote network protocol file.";
static const char *OPTION_IMAGE_URI_ILLEGAL_SCHEMA = "Error: Protocol `%s` is not supported.";

static const char *OPTION_MODE_REQUIRED = "Error: option `activate-mode` is required.";
static const char *OPTION_MODE_ILLEGAL = "Error: option `activate-mode` is illegal, available choices: Auto, Manual.";
static const char *OPTION_TYPE_ILLEGAL = "Error: option `firmware-type` is illegal, "
                                         "available choices: BMC, BIOS, CPLD, PSUFW.";

static const char *PRODUCT_SN_IS_NOT_SET = "Error: product SN is not correct.";
static const char *FAILED_TO_CREATE_FOLDER = "Error: failed to create log folder.";
static const char *FAILED_TO_CREATE_FILE = "Error: failed to create log file.";

static const char *const usage[] = {
        "fwupdate -u image-uri -e activate-mode [-t firmware-type]",
        NULL,
};

static void ValidateUpdateFirmwareOptions(UtoolUpdateFirmwareOption *updateFirmwareOption, UtoolResult *result);

static cJSON *BuildPayload(UtoolRedfishServer *server, UtoolUpdateFirmwareOption *updateFirmwareOption,
                           UtoolResult *result);

static int ResetBMCAndWaitingAlive(UtoolRedfishServer *server);

static void createUpdateLogFile(UtoolRedfishServer *server, UtoolUpdateFirmwareOption *updateFirmwareOption,
                                UtoolResult *result);

static void WriteLogEntry(UtoolUpdateFirmwareOption *option, const char *stage, const char *state, const char *note);

static void
WriteFailedLogEntry(UtoolUpdateFirmwareOption *option, const char *stage, const char *state, UtoolResult *result);

/**
 * 要求：升级失败要求能重试，最多3次。
 *
 * 如果升级BMC带了-e Auto参数，且出现BMC的HTTP不通或空间不足等情况时要求utool能直接先将BMC重启后再试。
 * 升级过程详细记录到升级日志文件，内容及格式详见sheet "升级日志文件"。
 *
 * @param self
 * @param option
 * @return
 */
int UtoolCmdUpdateOutbandFirmware(UtoolCommandOption *commandOption, char **outputStr)
{
    cJSON *output = NULL,           /** output result json*/
            *payload = NULL,        /** payload json */
            *updateFirmwareJson = NULL;    /** curl response thermal as json */

    UtoolResult *result = &(UtoolResult) {0};
    UtoolRedfishServer *server = &(UtoolRedfishServer) {0};
    UtoolCurlResponse *response = &(UtoolCurlResponse) {0};
    UtoolUpdateFirmwareOption *updateFirmwareOption = &(UtoolUpdateFirmwareOption) {0};

    struct argparse_option options[] = {
            OPT_BOOLEAN('h', "help", &(commandOption->flag), HELP_SUB_COMMAND_DESC, UtoolGetHelpOptionCallback, 0, 0),
            OPT_STRING ('u', "image-uri", &(updateFirmwareOption->imageURI), "firmware image file URI", NULL, 0, 0),
            OPT_STRING ('e', "activate-mode", &(updateFirmwareOption->activateMode),
                        "firmware active mode, choices: {Auto, Manual}", NULL, 0, 0),
            OPT_STRING ('t', "firmware-type", &(updateFirmwareOption->firmwareType),
                        "firmware type, choices: {BMC, BIOS, CPLD, PSUFW}",
                        NULL, 0, 0),
            OPT_END(),
    };

    // validation sub command options
    result->code = UtoolValidateSubCommandBasicOptions(commandOption, options, usage, &(result->desc));
    if (commandOption->flag != EXECUTABLE) {
        goto DONE;
    }

    // validation connection options
    result->code = UtoolValidateConnectOptions(commandOption, &(result->desc));
    if (commandOption->flag != EXECUTABLE) {
        goto DONE;
    }

    // get redfish system id
    result->code = UtoolGetRedfishServer(commandOption, server, &(result->desc));
    if (result->code != UTOOLE_OK || server->systemId == NULL) {
        goto FAILURE;
    }

    ZF_LOGI("Start update outband firmware progress now");

    /* create log file */
    createUpdateLogFile(server, updateFirmwareOption, result);


    /* validation update firmware options */
    ValidateUpdateFirmwareOptions(updateFirmwareOption, result);
    if (result->interrupt) {
        goto FAILURE;
    }

    int retryTimes = 0; /* current retry round */
    cJSON *cJSONTask = NULL;
    while (retryTimes++ < UPGRADE_FIRMWARE_RETRY_TIMES) {
        ZF_LOGI("Start to update outband firmware now, round: %d.", retryTimes);

        /* reset temp values */
        result->interrupt = 0;
        if (result->desc != NULL) {
            FREE_OBJ(result->desc);
        }

        if (result->data != NULL) {
            FREE_CJSON(result->data)
        }

        if (payload != NULL) {
            FREE_CJSON(payload)
        }


        char round[16];
        snprintf(round, sizeof(round), "Round %d", retryTimes);
        WriteLogEntry(updateFirmwareOption, STAGE_UPDATE, PROGRESS_START, round);

        /* step1: build payload - upload local file if necessary */
        payload = BuildPayload(server, updateFirmwareOption, result);
        if (payload == NULL || result->interrupt) {
            // TODO should we only retry when BMC return failure?
            if (result->retryable) {
                if (UtoolStringEquals(UPGRADE_ACTIVATE_MODE_AUTO, updateFirmwareOption->activateMode)) {
                    ZF_LOGI("Failed to upload file to BMC and activate mode is auto, will try reset BMC now.");
                    // we do not care about whether server is alive?
                    // ResetBMCAndWaitingAlive(server);
                }
            }
            continue;
        }

        /* step2: send update firmware request */
        char *url = "/UpdateService/Actions/UpdateService.SimpleUpdate";
        UtoolRedfishPost(server, url, payload, NULL, NULL, result);
        if (result->interrupt) {
            continue;
        }

        /* step3: Wait util download file progress finished */
        if (!updateFirmwareOption->isLocalFile) {
            ZF_LOGI("Waiting for BMC download update firmware file ...");
            WriteLogEntry(updateFirmwareOption, STAGE_DOWNLOAD_FILE, PROGRESS_START,
                          "Start download remote file to BMC");

            UtoolRedfishWaitUtilTaskStart(server, result->data, result);
            if (result->interrupt) {
                ZF_LOGE("Failed to download update firmware file.");
                WriteFailedLogEntry(updateFirmwareOption, STAGE_DOWNLOAD_FILE, PROGRESS_FAILED, result);
                continue;
            }

            ZF_LOGE("Download update firmware file successfully.");
            WriteLogEntry(updateFirmwareOption, STAGE_DOWNLOAD_FILE, PROGRESS_SUCCESS,
                          "Download remote file to BMC success");
        }


        sleep(3);

        /* step4: get firmware current version */
        //TODO "Motherboard CPLD"
        cJSON *firmwareType = cJSONUtils_GetPointer(result->data, "/Messages/MessageArgs/0");


        /* waiting util task complete or exception */
        UtoolRedfishWaitUtilTaskFinished(server, result->data, result);
        if (!result->interrupt) {
            break;
        }
        // TODO 是否需要判断task的状态？如果状态异常也是需要自动重试？
    }

    // if it still fails when reaching the maximum retries
    if (result->interrupt) {
        FREE_CJSON(result->data)
        goto FAILURE;
    }

    /* create output container */
    output = cJSON_CreateObject();
    result->code = UtoolAssetCreatedJsonNotNull(output);
    if (result->code != UTOOLE_OK) {
        goto FAILURE;
    }

    // output to result
    result->code = UtoolMappingCJSONItems(result->data, output, g_UtoolGetTaskMappings);
    FREE_CJSON(result->data)
    if (result->code != UTOOLE_OK) {
        goto FAILURE;
    }

    result->code = UtoolBuildOutputResult(STATE_SUCCESS, output, &(result->desc));
    if (result->code != UTOOLE_OK) {
        goto FAILURE;
    }

    goto DONE;

FAILURE:
    FREE_CJSON(output)
    goto DONE;

DONE:
    FREE_CJSON(payload)
    FREE_CJSON(updateFirmwareJson)
    UtoolFreeRedfishServer(server);
    UtoolFreeCurlResponse(response);

    if (updateFirmwareOption->logFileFP != NULL) {
        fseeko(updateFirmwareOption->logFileFP, -3, SEEK_END);
        __off_t position = ftello(updateFirmwareOption->logFileFP);
        ftruncate(fileno(updateFirmwareOption->logFileFP), position); /* delete last dot */

        fprintf(updateFirmwareOption->logFileFP, LOG_TAIL); /* write log file head content*/
        fclose(updateFirmwareOption->logFileFP);            /* close log file FP */
    }

    *outputStr = result->desc;
    return result->code;
}

/**
*
* create update log file&folder
*
* @param server
* @param updateFirmwareOption
* @param result
*/
static void createUpdateLogFile(UtoolRedfishServer *server, UtoolUpdateFirmwareOption *updateFirmwareOption,
                                UtoolResult *result)
{
    cJSON *getSystemRespJson = NULL;
    updateFirmwareOption->startTime = time(NULL);

    UtoolRedfishGet(server, "/Systems/%s", NULL, NULL, result);
    if (result->interrupt) {
        ZF_LOGE("Failed to load system resource.");
        goto FAILURE;
    }
    getSystemRespJson = result->data;

    cJSON *sn = cJSONUtils_GetPointer(getSystemRespJson, "/SerialNumber");
    if (sn != NULL && sn->valuestring != NULL) {
        ZF_LOGI("Parsing product SN, value is %s.", sn->valuestring);
        updateFirmwareOption->psn = sn->valuestring;
    }
    else {
        ZF_LOGE("Failed to get product SN, please make sure product SN is correct.");
        result->code = UtoolBuildOutputResult(STATE_FAILURE, cJSON_CreateString(PRODUCT_SN_IS_NOT_SET),
                                              &(result->desc));
        goto FAILURE;
    }

    char folderName[NAME_MAX];
    struct tm *tm_now = localtime(&updateFirmwareOption->startTime);
    snprintf(folderName, NAME_MAX, "%d%02d%02d%02d%02d%02d_%s", tm_now->tm_year + 1900, tm_now->tm_mon + 1,
             tm_now->tm_mday, tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec, updateFirmwareOption->psn);

    ZF_LOGE("Try to create folder for current updating, folder: %s.", folderName);
    int ret = mkdir(folderName, 0777);
    if (ret != 0) {
        result->code = UtoolBuildOutputResult(STATE_FAILURE, cJSON_CreateString(FAILED_TO_CREATE_FOLDER),
                                              &(result->desc));
        goto FAILURE;
    }


    char filepath[PATH_MAX];
    snprintf(filepath, PATH_MAX, "%s/update-firmware.log", folderName);
    updateFirmwareOption->logFileFP = fopen(filepath, "a");
    if (!updateFirmwareOption->logFileFP) {
        ZF_LOGW("Failed to create log file %s.", filepath);
        result->code = UtoolBuildOutputResult(STATE_FAILURE, cJSON_CreateString(FAILED_TO_CREATE_FILE),
                                              &(result->desc));
        goto FAILURE;
    }

    /* write log file head content*/
    fprintf(updateFirmwareOption->logFileFP, LOG_HEAD);

    goto DONE;

FAILURE:
    goto DONE;

DONE:
    FREE_CJSON(result->data)
}

static int ResetBMCAndWaitingAlive(UtoolRedfishServer *server)
{
    int ret;
    cJSON *payload;
    UtoolCurlResponse *resetManagerResp = &(UtoolCurlResponse) {0};

    ZF_LOGI("Restart BMC and waiting alive");

    // build payload
    char *payloadString = "{\"ResetType\" : \"ForceRestart\" }";
    payload = cJSON_CreateRaw(payloadString);
    ret = UtoolAssetCreatedJsonNotNull(payload);
    if (ret != UTOOLE_OK) {
        goto DONE;
    }

    // we do not care about whether reset manager is successful
    char *url = "/Managers/%s/Actions/Manager.Reset";
    UtoolMakeCurlRequest(server, url, HTTP_POST, payload, NULL, resetManagerResp);
    sleep(5);  // wait 5 seconds to let manager resetting takes effect

    ZF_LOGI("Waiting BMC alive now...");
    // waiting BMC up, total wait time is (30 + 1) * 30/2
    int interval = 30;
    while (interval > 0) {
        UtoolCurlResponse *getRedfishResp = &(UtoolCurlResponse) {0};
        ret = UtoolMakeCurlRequest(server, "/", HTTP_GET, NULL, NULL, getRedfishResp);
        if (ret == UTOOLE_OK && getRedfishResp->httpStatusCode < 300) {
            ZF_LOGI("BMC is alive now.");
            UtoolFreeCurlResponse(getRedfishResp);
            break;
        }

        ZF_LOGI("BMC is down now. Next check will be %d seconds later.", interval);
        sleep(interval);
        interval--;
        UtoolFreeCurlResponse(getRedfishResp);
    }

DONE:
    FREE_CJSON(payload)
    UtoolFreeCurlResponse(resetManagerResp);
    return ret;
}


/**
* validate user input options for update firmware command
*
* @param updateFirmwareOption
* @param result
* @return
*/
static void ValidateUpdateFirmwareOptions(UtoolUpdateFirmwareOption *updateFirmwareOption, UtoolResult *result)
{
    if (updateFirmwareOption->imageURI == NULL) {
        result->code = UtoolBuildOutputResult(STATE_FAILURE, cJSON_CreateString(OPTION_IMAGE_URI_REQUIRED),
                                              &(result->desc));
        goto FAILURE;
    }

    if (updateFirmwareOption->activateMode == NULL) {
        result->code = UtoolBuildOutputResult(STATE_FAILURE, cJSON_CreateString(OPTION_MODE_REQUIRED), &(result->desc));
        goto FAILURE;
    }

    if (!UtoolStringInArray(updateFirmwareOption->activateMode, MODE_CHOICES)) {
        result->code = UtoolBuildOutputResult(STATE_FAILURE, cJSON_CreateString(OPTION_MODE_ILLEGAL), &(result->desc));
        goto FAILURE;
    }

    if (updateFirmwareOption->firmwareType != NULL) {
        if (!UtoolStringInArray(updateFirmwareOption->firmwareType, TYPE_CHOICES)) {
            result->code = UtoolBuildOutputResult(STATE_FAILURE, cJSON_CreateString(OPTION_TYPE_ILLEGAL),
                                                  &(result->desc));
            goto FAILURE;
        }
    }


    /** try to treat imageURI as a local file */
    struct stat fileInfo;
    char *imageUri = updateFirmwareOption->imageURI;
    FILE *imageFileFP = fopen(imageUri, "rb"); /* open file to upload */
    updateFirmwareOption->isLocalFile = false;
    if (imageFileFP) {
        if (fstat(fileno(imageFileFP), &fileInfo) == 0) {
            updateFirmwareOption->isLocalFile = true;
        }
    }

    return;

FAILURE:
    result->interrupt = 1;
    return;
}

static cJSON *BuildPayload(UtoolRedfishServer *server, UtoolUpdateFirmwareOption *updateFirmwareOption,
                           UtoolResult *result)
{

    // build payload
    cJSON *payload = cJSON_CreateObject();
    result->code = UtoolAssetCreatedJsonNotNull(payload);
    if (result->code != UTOOLE_OK) {
        goto FAILURE;
    }

    char *imageUri = updateFirmwareOption->imageURI;

    // only output if not command help action is requested.
    ZF_LOGI("Update firmware options:");
    ZF_LOGI(" ");
    ZF_LOGI("\t\tImageURI\t\t : %s", imageUri);
    ZF_LOGI("\t\tActivateMode\t : %s", updateFirmwareOption->activateMode);
    ZF_LOGI("\t\tFirmwareType\t : %s", updateFirmwareOption->firmwareType);
    ZF_LOGI(" ");

    struct stat fileInfo;
    UtoolParsedUrl *parsedUrl = NULL;

    bool isLocalFile = false;
    /** try to treat imageURI as a local file */
    FILE *imageFileFP = fopen(imageUri, "rb"); /* open file to upload */
    if (imageFileFP) {
        if (fstat(fileno(imageFileFP), &fileInfo) == 0) {
            isLocalFile = true;
        }
    }

    if (isLocalFile) {  /** if file exists in local machine, try to upload it to BMC */
        ZF_LOGI("Firmware image uri `%s` is a local file.", imageUri);

        WriteLogEntry(updateFirmwareOption, STAGE_UPLOAD_FILE, PROGRESS_START, "");
        UtoolUploadFileToBMC(server, imageUri, result);
        if (result->interrupt) {
            WriteFailedLogEntry(updateFirmwareOption, STAGE_UPLOAD_FILE, PROGRESS_FAILED, result);
            goto FAILURE;
        }

        WriteLogEntry(updateFirmwareOption, STAGE_UPLOAD_FILE, PROGRESS_SUCCESS, "");

        char *filename = basename(imageUri);
        char uploadFilePath[MAX_FILE_PATH_LEN];
        snprintf(uploadFilePath, MAX_FILE_PATH_LEN, "/tmp/web/%s", filename);

        cJSON *imageUriNode = cJSON_AddStringToObject(payload, "ImageURI", uploadFilePath);
        result->code = UtoolAssetCreatedJsonNotNull(imageUriNode);
        if (result->code != UTOOLE_OK) {
            goto FAILURE;
        }

        goto DONE;
    }
    else if (UtoolStringStartsWith(imageUri, "/tmp/")) { /** handle BMC tmp file */
        cJSON *imageUriNode = cJSON_AddStringToObject(payload, "ImageURI", imageUri);
        result->code = UtoolAssetCreatedJsonNotNull(imageUriNode);
        if (result->code != UTOOLE_OK) {
            goto FAILURE;
        }
        goto DONE;
    }
    else { /** handle remote file */
        ZF_LOGI("Firmware image uri `%s` is not local file, will start update firmware directly now.", imageUri);

        /** parse url */
        parsedUrl = UtoolParseURL(imageUri);

        if (!parsedUrl || !parsedUrl->scheme) {
            ZF_LOGI("It seems the image uri is illegal.");
            result->code = UtoolBuildOutputResult(STATE_FAILURE, cJSON_CreateString(OPTION_IMAGE_URI_NO_SCHEMA),
                                                  &(result->desc));
            WriteLogEntry(updateFirmwareOption, STAGE_UPLOAD_FILE, PROGRESS_INVAILD_URI, OPTION_IMAGE_URI_NO_SCHEMA);
            goto FAILURE;
        }

        if (!UtoolStringCaseInArray(parsedUrl->scheme, IMAGE_PROTOCOL_CHOICES)) {
            char message[MAX_FAILURE_MSG_LEN];
            snprintf(message, MAX_FAILURE_MSG_LEN, OPTION_IMAGE_URI_ILLEGAL_SCHEMA, parsedUrl->scheme);
            result->code = UtoolBuildOutputResult(STATE_FAILURE, cJSON_CreateString(message),
                                                  &(result->desc));
            WriteLogEntry(updateFirmwareOption, STAGE_UPLOAD_FILE, PROGRESS_INVAILD_URI, message);
            goto FAILURE;
        }

        cJSON *imageUriNode = cJSON_AddStringToObject(payload, "ImageURI", imageUri);
        result->code = UtoolAssetCreatedJsonNotNull(imageUriNode);
        if (result->code != UTOOLE_OK) {
            goto FAILURE;
        }

        UtoolStringToUpper(parsedUrl->scheme);
        cJSON *protocolNode = cJSON_AddStringToObject(payload, "TransferProtocol", parsedUrl->scheme);
        result->code = UtoolAssetCreatedJsonNotNull(protocolNode);
        if (result->code != UTOOLE_OK) {
            goto FAILURE;
        }
    }

    goto DONE;

FAILURE:
    result->interrupt = 1;
    FREE_CJSON(payload)
    payload = NULL;
    goto DONE;

DONE:
    if (imageFileFP) {                  /* close FP */
        fclose(imageFileFP);
    }
    UtoolFreeParsedURL(parsedUrl);
    return payload;
}


static void
WriteFailedLogEntry(UtoolUpdateFirmwareOption *option, const char *stage, const char *state, UtoolResult *result)
{
    if (result->code != UTOOLE_OK) {
        const char *errorString = (result->code > UTOOLE_OK && result->code < CURL_LAST) ?
                                  curl_easy_strerror(result->code) : UtoolGetStringError(result->code);
        WriteLogEntry(option, stage, state, errorString);
        return;
    }
    else {
        cJSON *output = cJSON_Parse(result->desc);
        if (output != NULL) {
            cJSON *message = cJSONUtils_GetPointer(output, "/Message/0");
            WriteLogEntry(option, stage, state, message == NULL ? "" : message->valuestring);
        }
        FREE_CJSON(output)
    }
}


static void WriteLogEntry(UtoolUpdateFirmwareOption *option, const char *stage, const char *state, const char *note)
{
    /* get current timestamp */
    char nowStr[100];
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    strftime(nowStr, sizeof(nowStr) - 1, "%Y%m%dT%H%M%S%z", tm_now);

    //char entry[MAX_LOG_ENTRY_LEN];
    //snprintf(entry, MAX_LOG_ENTRY_LEN, LOG_ENTRY_FORMAT, nowStr, stage, state, note);

    /* write log file head content*/
    fprintf(option->logFileFP, LOG_ENTRY_FORMAT, nowStr, stage, state, note);
    fflush(option->logFileFP);
}

// {\n\t"Time": "%s", \n\t"Stage": "%s",\n\t"State": "%s","Note": \n\t"%s"\n}