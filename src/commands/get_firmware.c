/*
* Copyright © Huawei Technologies Co., Ltd. 2018-2019. All rights reserved.
* Description: command handler for `getfw`
* Author:
* Create: 2019-06-14
* Notes:
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "string_utils.h"
#include "cJSON_Utils.h"
#include "commons.h"
#include "curl/curl.h"
#include "zf_log.h"
#include "constants.h"
#include "command-helps.h"
#include "command-interfaces.h"
#include "argparse.h"
#include "redfish.h"

static const char *const usage[] = {
        "getfw",
        NULL,
};

static int FirmwareTypeHandler(cJSON *target, const char *key, cJSON *node) {
    if (cJSON_IsNull(node)) {
        cJSON_AddItemToObjectCS(target, key, node);
        return UTOOLE_OK;
    }

    // it seems firmware name is not solid enough to parse Type
    // UtoolStringEndsWith(node->valuestring, "BMC");
    // we will try to parse Type from Software-Id
    char *type = strtok(node->valuestring, "-");
    cJSON *newNode = cJSON_AddStringToObject(target, key, type);
    FREE_CJSON(node)
    int ret = UtoolAssetCreatedJsonNotNull(newNode);
    if (ret != UTOOLE_OK) {
        return ret;
    }

    /**
        BIOS：dcpowercycle
        CPLD：dcpowercycle
        iBMC：automatic
        PSU：automatic
        FAN：automatic
        FW：none
        Driver：none
    */
    char *activateType = NULL;
    if (UtoolStringEquals(newNode->valuestring, "BMC")) {
        activateType = "[\"automatic\"]";
    }
    else if (UtoolStringEquals(newNode->valuestring, "CPLD")) {
        activateType = "[\"dcpowercycle\"]";
    }
    else if (UtoolStringEquals(newNode->valuestring, "BIOS")) {
        activateType = "[\"dcpowercycle\"]";
    }
    cJSON *supportActivateTypes = cJSON_AddRawToObject(target, "SupportActivateType", activateType);
    ret = UtoolAssetCreatedJsonNotNull(supportActivateTypes);
    if (ret != UTOOLE_OK) {
        return ret;
    }

    return ret;
}

static int VersionHandler(cJSON *target, const char *key, cJSON *node) {
    if (cJSON_IsNull(node)) {
        cJSON_AddItemToObjectCS(target, key, node);
        return UTOOLE_OK;
    }

    char *version = node->valuestring;
    char **segments = UtoolStringSplit(version, '.');

    char *first = *segments;
    char *second = *(segments + 1) == NULL ? "0" : *(segments + 1);
    char *third = *(segments + 2) == NULL ? "0" : *(segments + 2);

    char output[16] = {0};
    snprintf(output, sizeof(output), "%s.%02ld.%02ld", first, strtol(second, NULL, 0), strtol(third, NULL, 0));

    for (int idx = 0; *(segments + idx); idx++) {
        free(*(segments + idx));
    }
    free(segments);

    cJSON *newNode = cJSON_AddStringToObject(target, key, output);
    FREE_CJSON(node)
    return UtoolAssetCreatedJsonNotNull(newNode);
}

static const UtoolOutputMapping getFwMappings[] = {
        {.sourceXpath = "/Name", .targetKeyValue="Name"},
        {.sourceXpath = "/SoftwareId", .targetKeyValue="Type", .handle=FirmwareTypeHandler},
        {.sourceXpath = "/Version", .targetKeyValue="Version", .handle=VersionHandler},
        {.sourceXpath = "/Updateable", .targetKeyValue="Updateable"},
        //{.sourceXpath = "/SoftwareId", .targetKeyValue="SupportActivateType"},
        {0}
};


/**
 * Get outband firmware information, command handler of `getfw`
 *
 * @param commandOption
 * @param result
 * @return
 */
int UtoolCmdGetFirmware(UtoolCommandOption *commandOption, char **result) {
    int ret;

    struct argparse_option options[] = {
            OPT_BOOLEAN('h', "help", &(commandOption->flag), HELP_SUB_COMMAND_DESC, UtoolGetHelpOptionCallback, 0, 0),
            OPT_END(),
    };

    UtoolRedfishServer *server = &(UtoolRedfishServer) {0};
    UtoolCurlResponse *memberResp = &(UtoolCurlResponse) {0};
    UtoolCurlResponse *firmwareResp = &(UtoolCurlResponse) {0};

    // initialize output objects
    cJSON *firmwareJson = NULL, *firmwareMembersJson = NULL;
    cJSON *output = NULL, *firmwares = NULL, *firmware = NULL;

    ret = UtoolValidateSubCommandBasicOptions(commandOption, options, usage, result);
    if (commandOption->flag != EXECUTABLE) {
        goto DONE;
    }

    ret = UtoolValidateConnectOptions(commandOption, result);
    if (commandOption->flag != EXECUTABLE) {
        goto DONE;
    }

    ret = UtoolGetRedfishServer(commandOption, server, result);
    if (ret != UTOOLE_OK || server->systemId == NULL) {
        goto DONE;
    }

    ret = UtoolMakeCurlRequest(server, "/UpdateService/FirmwareInventory", HTTP_GET, NULL, NULL, memberResp);
    if (ret != UTOOLE_OK) {
        goto DONE;
    }
    if (memberResp->httpStatusCode >= 400) {
        ret = UtoolResolveFailureResponse(memberResp, result);
        goto DONE;
    }

    output = cJSON_CreateObject();
    ret = UtoolAssetCreatedJsonNotNull(output);
    if (ret != UTOOLE_OK) {
        goto FAILURE;
    }

    firmwares = cJSON_AddArrayToObject(output, "Firmware");
    ret = UtoolAssetCreatedJsonNotNull(firmwares);
    if (ret != UTOOLE_OK) {
        goto FAILURE;
    }

    // process response
    firmwareMembersJson = cJSON_Parse(memberResp->content);
    ret = UtoolAssetParseJsonNotNull(firmwareMembersJson);
    if (ret != UTOOLE_OK) {
        goto FAILURE;
    }

    cJSON *members = cJSON_GetObjectItem(firmwareMembersJson, "Members");
    int memberCount = cJSON_GetArraySize(members);
    for (int idx = 0; idx < memberCount; idx++) {
        cJSON *member = cJSON_GetArrayItem(members, idx);
        cJSON *odataIdNode = cJSON_GetObjectItem(member, "@odata.id");
        char *url = odataIdNode->valuestring;
        if (UtoolStringEndsWith(url, "BMC") || UtoolStringEndsWith(url, "Bios") || UtoolStringEndsWith(url, "CPLD")) {
            ret = UtoolMakeCurlRequest(server, url, HTTP_GET, NULL, NULL, firmwareResp);
            if (ret != UTOOLE_OK) {
                goto FAILURE;
            }

            if (firmwareResp->httpStatusCode >= 400) {
                ret = UtoolResolveFailureResponse(firmwareResp, result);
                goto FAILURE;
            }

            firmwareJson = cJSON_Parse(firmwareResp->content);
            ret = UtoolAssetParseJsonNotNull(firmwareJson);
            if (ret != UTOOLE_OK) {
                goto FAILURE;
            }

            firmware = cJSON_CreateObject();
            ret = UtoolAssetCreatedJsonNotNull(firmware);
            if (ret != UTOOLE_OK) {
                goto FAILURE;
            }

            // create firmware item and add it to array
            ret = UtoolMappingCJSONItems(firmwareJson, firmware, getFwMappings);
            if (ret != UTOOLE_OK) {
                goto FAILURE;
            }
            cJSON_AddItemToArray(firmwares, firmware);

            // free memory
            FREE_CJSON(firmwareJson)
            UtoolFreeCurlResponse(firmwareResp);
        }
    }


    // output to result
    ret = UtoolBuildOutputResult(STATE_SUCCESS, output, result);
    goto DONE;

FAILURE:
    FREE_CJSON(firmware)
    FREE_CJSON(output)
    goto DONE;

DONE:
    FREE_CJSON(firmwareMembersJson)
    FREE_CJSON(firmwareJson)
    UtoolFreeCurlResponse(memberResp);
    UtoolFreeCurlResponse(firmwareResp);
    UtoolFreeRedfishServer(server);
    return ret;
}
