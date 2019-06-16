/*
* Copyright © Huawei Technologies Co., Ltd. 2012-2018. All rights reserved.
* Description: common utilities header
* Author:
* Create: 2019-06-16
* Notes:
*/
#ifndef UTOOL_COMMONS_H
#define UTOOL_COMMONS_H
/* For c++ compatibility */

#ifdef __cplusplus
extern "C" {
#endif

#include <cJSON.h>
#include "typedefs.h"
#include "constants.h"
#include "zf_log.h"

#define FREE_CJSON(json) if ((json) != NULL) { cJSON_Delete((json)); (json) = NULL; }
#define FREE_OBJ(obj) if ((obj) != NULL) { free((obj)); (obj) = NULL; }

/**
* choices: Enabled, Disabled
*/
extern const char *g_UTOOL_ENABLED_CHOICES[];

/**
*   All command metadata define
*/
extern UtoolCommand g_UtoolCommands[];


extern const char *g_UtoolRedfishTaskSuccessStatus[];

extern const char *g_UtoolRedfishTaskFinishedStatus[];


/**
* Redfish Async Task output mapping
*/
extern const UtoolOutputMapping g_UtoolGetTaskMappings[];

extern int UtoolBoolToEnabledPropertyHandler(cJSON *target, const char *key, cJSON *node);


static inline int UtoolFreeRedfishTask(UtoolRedfishTask *task)
{
    if (task != NULL) {
        FREE_OBJ(task->url)
        FREE_OBJ(task->id)
        FREE_OBJ(task->name)
        FREE_OBJ(task->taskState)
        FREE_OBJ(task->startTime)
        FREE_OBJ(task->taskPercentage)

        FREE_OBJ(task->message->id)
        FREE_OBJ(task->message->message)
        FREE_OBJ(task->message->severity)
        FREE_OBJ(task->message->resolution)
        FREE_OBJ(task->message)
        FREE_OBJ(task)
    }
}

/**
* Free Utool Result object
*
* - remember desc should not be freed, it's always used by output string.
*
* @param result
* @return
*/
static inline int UtoolFreeUtoolResult(UtoolResult *result)
{
    if (result != NULL) {
        FREE_CJSON(result->data)
    }
}

/**
 * Free redfish server struct
 *
 * @param self
 * @param option
 * @return
 */
static inline int UtoolFreeRedfishServer(UtoolRedfishServer *server)
{
    if (server != NULL) {
        FREE_OBJ(server->host)
        FREE_OBJ(server->baseUrl)
        FREE_OBJ(server->username)
        FREE_OBJ(server->password)
        FREE_OBJ(server->systemId)
    }
}

/**
 * Free redfish server struct
 *
 * @param self
 * @param option
 * @return
 */
static inline int UtoolFreeCurlResponse(UtoolCurlResponse *response)
{
    if (response != NULL) {
        FREE_OBJ(response->content)
        FREE_OBJ(response->etag)
        FREE_OBJ(response->contentType)
    }
}

/**
 * asset json object is not null
 *
 * @param self
 * @param option
 * @return
 */
static inline int UtoolAssetCreatedJsonNotNull(cJSON *json)
{
    if (json == NULL) {
        ZF_LOGE("Failed to create JSON object");
        return UTOOLE_CREATE_JSON_NULL;
    }
    return UTOOLE_OK;
}

/**
 * asset json object is not null
 *
 * @param self
 * @param option
 * @return
 */
static inline int UtoolAssetParseJsonNotNull(cJSON *json)
{
    if (json == NULL || cJSON_IsNull(json)) {
        ZF_LOGE("Failed to parse JSON content into JSON object.");
        return UTOOLE_PARSE_JSON_FAILED;
    }
    return UTOOLE_OK;
}


/**
 * asset json node object is not null
 *
 * @param json
 * @param xpath
 * @return
 */
static inline int UtoolAssetJsonNodeNotNull(cJSON *node, char *xpath)
{
    if (node == NULL || cJSON_IsNull(node)) {
        ZF_LOGE("Failed to get the node(%s) from json", xpath);
        return UTOOLE_UNKNOWN_JSON_FORMAT;
    }
    return UTOOLE_OK;
}

/**
 * asset print JSON object to string is not null
 *
 * @param json
 * @param xpath
 * @return
 */
static inline int UtoolAssetPrintJsonNotNull(char *content)
{
    if (content == NULL) {
        ZF_LOGE("Failed to print JSON object to string");
        return UTOOLE_PRINT_JSON_FAILED;
    }
    return UTOOLE_OK;
}


/**
 * build new json result and assign to (char **) result
 *
 * @param state
 * @param messages
 * @param result
 * @return
 */
int UtoolBuildOutputResult(const char *state, cJSON *messages, char **result);

/**
 *
 * build new json result and assign to (char **) result.
 * Be aware that messages will be free at last
 *
 * @param state
 * @param messages
 * @param result
 * @return
 */
int UtoolBuildStringOutputResult(const char *state, const char *messages, char **result);

/**
* build output result according to redfish task and Assign output to result.
*
* @param task
* @param result
* @return
*/
int UtoolBuildRsyncTaskOutputResult(cJSON *task, char **result);

/**
 *
 * @param source
 * @param target
 * @param mapping
 * @return
 */
int UtoolMappingCJSONItems(cJSON *source, cJSON *target, const UtoolOutputMapping *mappings);

/**
* build default success output result
*
* @param result
*/
void UtoolBuildDefaultSuccessResult(char **result);


/**
* wrap a cJSON object with Huawei Oem structure
*
* @param source
* @param result
* @return
*/
cJSON *UtoolWrapOem(cJSON *source, UtoolResult *result);


/**
* calculate overall health from item health state
*
* @param items
* @param xpath
* @return
*/
char *UtoolGetOverallHealth(cJSON *items, const char *xpath);


/**
* get string error of UtoolCode
*
* @param code
* @return
*/
const char *UtoolGetStringError(UtoolCode code);


#ifdef __cplusplus
}
#endif //UTOOL_COMMONS_H
#endif
