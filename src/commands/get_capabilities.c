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
        "utool getcapabilities",
        NULL,
};

/**
 * argparse get capabilities action callback
 *
 * @param self
 * @param option
 * @return
 */
int UtoolCmdGetCapabilities(UtoolCommandOption *commandOption, char **result)
{
    int ret;

    struct argparse_option options[] = {
            OPT_BOOLEAN('h', "help", &(commandOption->flag), HELP_SUB_COMMAND_DESC, UtoolGetHelpOptionCallback, 0, 0),
            OPT_END(),
    };

    ret = UtoolValidateSubCommandBasicOptions(commandOption, options, usage, result);
    if (commandOption->flag != EXECUTABLE) {
        goto DONE;
    }

    cJSON *output, *getCommandList, *setCommandList;

    output = cJSON_CreateObject();
    ret = UtoolAssetCreatedJsonNotNull(output);
    if (ret != UTOOLE_OK) {
        goto FAILURE;
    }

    getCommandList = cJSON_AddArrayToObject(output, "GetCommandList");
    setCommandList = cJSON_AddArrayToObject(output, "SetCommandList");

    ret = UtoolAssetCreatedJsonNotNull(setCommandList);
    if (ret != UTOOLE_OK) {
        goto FAILURE;
    }

    ret = UtoolAssetCreatedJsonNotNull(output);
    if (ret != UTOOLE_OK) {
        goto FAILURE;
    }


    for (int idx = 0;; idx++) {
        UtoolCommand *command = utoolCommands + idx;
        if (command->name != NULL) {
            switch (command->type) {
                case GET:
                    cJSON_AddItemToArray(getCommandList, cJSON_CreateString(command->name));
                    break;
                case SET:
                    cJSON_AddItemToArray(setCommandList, cJSON_CreateString(command->name));
                    break;
                default:
                    break;
            }
        }
        else {
            break;
        }
    }

    ret = UtoolBuildOutputResult(STATE_SUCCESS, output, result);
    goto DONE;

FAILURE:
    FREE_CJSON(output)
    goto DONE;

DONE:
    return ret;
}
