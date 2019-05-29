//
// Created by qianbiao on 5/10/19.
//

#ifndef UTOOL_REDFISH_H
#define UTOOL_REDFISH_H
/* For c++ compatibility */
#ifdef __cplusplus
extern "C" {
#endif

#include "typedefs.h"
#include "zf_log.h"

static const char *const HTTP_GET = "GET";
static const char *const HTTP_POST = "POST";
static const char *const HTTP_PATCH = "PATCH";
static const char *const HTTP_PUT = "PUT";
static const char *const HTTP_DELETE = "DELETE";


/**
 * get redfish server information from command options
 *
 * @param option
 * @param server
 * @return
 */
int UtoolGetRedfishServer(UtoolCommandOption *option, UtoolRedfishServer *server, char **result);


/**
* Upload file to BMC temp storage through CURL lib
* @param server
* @param uploadFilePath
* @param response
* @return
*/
int UtoolUploadFileToBMC(UtoolRedfishServer *server, const char *uploadFilePath, UtoolCurlResponse *response);

/**
* download file from BMC temp storage to a local file through CURL lib
* @param server
* @param bmcFileUri
* @param localFileUri
* @param response
* @return
*/
int UtoolDownloadFileFromBMC(UtoolRedfishServer *server, const char *bmcFileUri, const char *localFileUri,
                             UtoolCurlResponse *response);

/**
 * Make a new redfish request through CURL lib
 *
 * @param server
 * @param resourceURL
 * @param httpMethod
 * @param payload
 * @param response a pointer passing response of the request
 * @return
 */
int UtoolMakeCurlRequest(UtoolRedfishServer *server,
                         char *resourceURL,
                         const char *httpMethod,
                         const cJSON *payload,
                         const UtoolCurlHeader *headers,
                         UtoolCurlResponse *response);

/**
 * resolve redfish failures response
 *
 * @param response
 * @param result
 * @return
 */
int UtoolResolveFailureResponse(UtoolCurlResponse *response, char **result);


/**
 * generate failure message array from redfish failure response
 *
 * @param response
 * @param failures
 * @return
 */
int UtoolGetFailuresFromResponse(UtoolCurlResponse *response, cJSON *failures);

/**
* Get Redfish Resource
*
* @param server
* @param url
* @param result
*/
void UtoolRedfishGetResource(UtoolRedfishServer *server, char *url, UtoolResult *result);

/**
* Get All Redfish member resources
*
* @param server
* @param owner
* @param memberArray
* @param memberMapping
* @param result
*/
void UtoolRedfishGetMemberResources(UtoolRedfishServer *server, cJSON *owner, cJSON *memberArray,
                                    const UtoolOutputMapping *memberMapping, UtoolResult *result);


#ifdef __cplusplus
}
#endif //UTOOL_REDFISH_H
#endif
