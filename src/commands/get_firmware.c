//
// Created by qianbiao on 5/8/19.
//
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
        "utool getfw",
        NULL,
};

static int FirmwareTypeHandler(cJSON *target, const char *key, cJSON *node)
{
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
    return UtoolAssetCreatedJsonNotNull(newNode);
}


static const UtoolOutputMapping getFwMappings[] = {
        {.sourceXpath = "/Name", .targetKeyValue="Name"},
        {.sourceXpath = "/SoftwareId", .targetKeyValue="Type", .handle=FirmwareTypeHandler},
        {.sourceXpath = "/Version", .targetKeyValue="Version"},
        {.sourceXpath = "/Updateable", .targetKeyValue="Updateable"},
        //{.sourceXpath = "", .targetKeyValue="SupportActivateType"},
        NULL
};


/**
 * command handler of `getfw`
 *
 * @param commandOption
 * @param result
 * @return
 */
int UtoolCmdGetFirmware(UtoolCommandOption *commandOption, char **result)
{
    int ret;

    struct argparse_option options[] = {
            OPT_BOOLEAN('h', "help", &(commandOption->flag), HELP_SUB_COMMAND_DESC, UtoolGetHelpOptionCallback, 0, 0),
            OPT_END(),
    };

    UtoolRedfishServer *server = &(UtoolRedfishServer) {0};
    UtoolCurlResponse *memberResp = &(UtoolCurlResponse) {0};
    UtoolCurlResponse *firmwareResp = &(UtoolCurlResponse) {0};

    ret = UtoolValidateSubCommandBasicOptions(commandOption, options, usage, result);
    if (commandOption->flag != EXECUTABLE) {
        goto done;
    }

    ret = UtoolValidateConnectOptions(commandOption, result);
    if (commandOption->flag != EXECUTABLE) {
        goto done;
    }

    ret = UtoolGetRedfishServer(commandOption, server, result);
    if (ret != UTOOLE_OK || server->systemId == NULL) {
        goto done;
    }

    ret = UtoolMakeCurlRequest(server, "/UpdateService/FirmwareInventory", HTTP_GET, NULL, NULL, memberResp);
    if (ret != UTOOLE_OK) {
        goto done;
    }
    if (memberResp->httpStatusCode >= 400) {
        ret = UtoolResolveFailureResponse(memberResp, result);
        goto done;
    }

    // initialize output objects
    cJSON *firmwareJson = NULL, *firmwareMembersJson = NULL;
    cJSON *output = NULL, *firmwares = NULL, *firmware = NULL;

    output = cJSON_CreateObject();
    ret = UtoolAssetCreatedJsonNotNull(output);
    if (ret != UTOOLE_OK) {
        goto failure;
    }

    firmwares = cJSON_AddArrayToObject(output, "Firmware");
    ret = UtoolAssetCreatedJsonNotNull(firmwares);
    if (ret != UTOOLE_OK) {
        goto failure;
    }

    // process response
    firmwareMembersJson = cJSON_Parse(memberResp->content);
    ret = UtoolAssetParseJsonNotNull(firmwareMembersJson);
    if (ret != UTOOLE_OK) {
        goto failure;
    }

    cJSON *members = cJSON_GetObjectItem(firmwareMembersJson, "Members");
    int memberCount = cJSON_GetArraySize(members);
    for (int idx = 0; idx < memberCount; idx++) {
        cJSON *member = cJSON_GetArrayItem(members, idx);
        cJSON *odataIdNode = cJSON_GetObjectItem(member, "@odata.id");
        char *url = odataIdNode->valuestring;

        ret = UtoolMakeCurlRequest(server, url, HTTP_GET, NULL, NULL, firmwareResp);
        if (ret != UTOOLE_OK) {
            goto failure;
        }

        if (firmwareResp->httpStatusCode >= 400) {
            ret = UtoolResolveFailureResponse(firmwareResp, result);
            goto failure;
        }

        firmwareJson = cJSON_Parse(firmwareResp->content);
        ret = UtoolAssetParseJsonNotNull(firmwareJson);
        if (ret != UTOOLE_OK) {
            goto failure;
        }

        firmware = cJSON_CreateObject();
        ret = UtoolAssetCreatedJsonNotNull(firmware);
        if (ret != UTOOLE_OK) {
            goto failure;
        }

        cJSON_AddRawToObject(firmware, "SupportActivateType", "[\"automatic\"]");
        //cJSON *supportActivateTypes = cJSON_AddArrayToObject(firmware, "SupportActivateType");
        //ret = UtoolAssetCreatedJsonNotNull(supportActivateTypes);
        //if (ret != UTOOLE_OK) {
        //    goto failure;
        //}

        // create firmware item and add it to array
        ret = UtoolMappingCJSONItems(firmwareJson, firmware, getFwMappings);
        if (ret != UTOOLE_OK) {
            goto failure;
        }
        cJSON_AddItemToArray(firmwares, firmware);

        // free memory
        FREE_CJSON(firmwareJson)
        UtoolFreeCurlResponse(firmwareResp);
    }


    // output to result
    ret = UtoolBuildOutputResult(STATE_SUCCESS, output, result);
    goto done;

failure:
    FREE_CJSON(firmware)
    FREE_CJSON(output)
    goto done;

done:
    FREE_CJSON(firmwareMembersJson)
    FREE_CJSON(firmwareJson)
    UtoolFreeCurlResponse(memberResp);
    UtoolFreeCurlResponse(firmwareResp);
    UtoolFreeRedfishServer(server);
    return ret;
}
