#include "../main.h"
#include "../errors.h"

#include "web.h"
#include "json.h"

#include <libwebsockets.h>
#include <stdio.h> // Debug
#include <errno.h> // sleep_ms - EINTR

#define HTDOCS_DIR "./web/htdocs"

#define WEBSOCKET_OUTPUT_LENGTH 16384 // characters
typedef struct {
    uint8_t buffer[LWS_PRE+WEBSOCKET_OUTPUT_LENGTH];
    uint32_t length;
    uint32_t sequence_id;
    pthread_mutex_t mutex;
    bool new; // Not locked by mutex
} websocket_output_t;

websocket_output_t ws_monitor_output = {
    .length = WEBSOCKET_OUTPUT_LENGTH,
    .sequence_id = 1,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .new = false
};

typedef struct websocket_user_session_t websocket_user_session_t;

struct websocket_user_session_t {
    struct lws *wsi;
    websocket_user_session_t *websocket_user_session_list;
    uint32_t last_sequence_id;
};

typedef struct {
    struct lws_context *context;
    struct lws_vhost *vhost;
    const struct lws_protocols *protocol;
    websocket_user_session_t *websocket_user_session_list;
} websocket_vhost_session_t;

enum protocol_ids {
    HTTP = 0,
    WS_MONITOR = 1,
    WS_CONTROL = 2,
    _TERMINATOR = 99
};

/* -------------------------------------------------------------------------------------------------- */
uint64_t timestamp_ms(void) {
/* -------------------------------------------------------------------------------------------------- */
/* Returns current value of a realtime timer in milliseconds                                         */
/* return: realtime timer in milliseconds                                                            */
/* -------------------------------------------------------------------------------------------------- */
    struct timespec tp;

    if(clock_gettime(CLOCK_REALTIME, &tp) != 0)
    {
        return 0;
    }

    return (uint64_t) tp.tv_sec * 1000 + tp.tv_nsec / 1000000;
}

/* -------------------------------------------------------------------------------------------------- */
void sleep_ms(uint32_t _duration)
/* -------------------------------------------------------------------------------------------------- */
/* Pauses the current thread for a given duration in milliseconds                                     */
/*                                                                                                    */
/* -------------------------------------------------------------------------------------------------- */
{
    struct timespec req, rem;
    req.tv_sec = _duration / 1000;
    req.tv_nsec = (_duration - (req.tv_sec*1000))*1000*1000;

    while(nanosleep(&req, &rem) == EINTR)
    {
        req = rem;
    }
}

int callback_ws(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    int32_t n;
    websocket_user_session_t *user_session = (websocket_user_session_t *)user;

    websocket_vhost_session_t *vhost_session =
            (websocket_vhost_session_t *)
            lws_protocol_vh_priv_get(lws_get_vhost(wsi),
                    lws_get_protocol(wsi));

    switch (reason)
    {
        case LWS_CALLBACK_PROTOCOL_INIT:
            vhost_session = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi),
                    lws_get_protocol(wsi),
                    sizeof(websocket_vhost_session_t));
            vhost_session->context = lws_get_context(wsi);
            vhost_session->protocol = lws_get_protocol(wsi);
            vhost_session->vhost = lws_get_vhost(wsi);
            break;

        case LWS_CALLBACK_ESTABLISHED:
            /* add ourselves to the list of live pss held in the vhd */
            lws_ll_fwd_insert(
                user_session,
                websocket_user_session_list,
                vhost_session->websocket_user_session_list
            );
            user_session->wsi = wsi;
            //user_session->last = vhost_session->current;
            break;

        case LWS_CALLBACK_CLOSED:
            /* remove our closing pss from the list of live pss */
            lws_ll_fwd_remove(
                websocket_user_session_t,
                websocket_user_session_list,
                user_session,
                vhost_session->websocket_user_session_list
            );
            break;


        case LWS_CALLBACK_SERVER_WRITEABLE:
            /* Write output data, if data exists */
            /* Look up protocol */
            if(vhost_session->protocol->id == WS_MONITOR)
            {
                pthread_mutex_lock(&ws_monitor_output.mutex);
                if(ws_monitor_output.length != 0 && user_session->last_sequence_id != ws_monitor_output.sequence_id)
                {
                    n = lws_write(wsi, (unsigned char*)&ws_monitor_output.buffer[LWS_PRE], ws_monitor_output.length, LWS_WRITE_TEXT);
                    if (!n)
                    {
                        pthread_mutex_unlock(&ws_monitor_output.mutex);
                        lwsl_err("ERROR %d writing to socket\n", n);
                        return -1;
                    }
                    user_session->last_sequence_id = ws_monitor_output.sequence_id;
                }
                pthread_mutex_unlock(&ws_monitor_output.mutex);
            }
            break;

        case LWS_CALLBACK_RECEIVE:
            if(len >= 8 && strcmp((const char *)in, "closeme\n") == 0)
            {
                lws_close_reason(wsi, LWS_CLOSE_STATUS_GOINGAWAY,
                         (unsigned char *)"seeya", 5);
                return -1;
            }
            if(vhost_session->protocol->id == WS_CONTROL)
            {
                if(len >= 3 && len < 32)
                {
                    char message_string[32];
                    memcpy(message_string, in, len);
                    message_string[len] = '\0';

                    //printf("RX: %s\n", message_string);

                    if(message_string[0] == 'C')
                    {
                        /* Combined Command (frequency and symbolrate) */
                        char *field2_ptr;
                        uint32_t freq, sr;

                        /* Find field divider */
                        field2_ptr = memchr(message_string, ',', len);
                        if(field2_ptr != NULL)
                        {
                            /* Divide the strings */
                            field2_ptr[0] = '\0';

                            freq = (uint32_t)strtol(&message_string[1],NULL,10);
                            sr = (uint32_t)strtol(&field2_ptr[1],NULL,10);
                            config_set_frequency_and_symbolrate(freq, sr);
                        }
                    }
                    else if(message_string[0] == 'F')
                    {
                        /* Frequency Command */
                        uint32_t freq;
                        freq = (uint32_t)strtol(&message_string[1],NULL,10);
                        config_set_frequency(freq);
                    }
                    else if(message_string[0] == 'S')
                    {
                        /* Symbolrate Command */
                        uint32_t sr;
                        sr = (uint32_t)strtol(&message_string[1],NULL,10);
                        config_set_symbolrate(sr);
                    }
                    else if(message_string[0] == 'V')
                    {
                        /* LNB Voltage Supply Command */
                        char *field2_ptr;
                        bool lnbv_enabled, lnbv_horizontal;

                        /* Find field divider */
                        field2_ptr = memchr(message_string, ',', len);
                        if(field2_ptr != NULL)
                        {
                            /* Divide the strings */
                            field2_ptr[0] = '\0';

                            lnbv_enabled = (bool)!!strtol(&message_string[1],NULL,10);
                            lnbv_horizontal = (bool)!!strtol(&field2_ptr[1],NULL,10);
                            config_set_lnbv(lnbv_enabled, lnbv_horizontal);
                        }
                    }
                }
            }
            break;
        
        default:
            break;
    }

    return 0;
}

static struct lws_protocols protocols[] = {
    {
        .id = 0,
        .name = "http",
        .callback = lws_callback_http_dummy,
        .per_session_data_size = 0,
        .rx_buffer_size = 0,
    },
    {
        .id = 1,
        .name = "monitor",
        .callback = callback_ws,
        .per_session_data_size = 128,
        .rx_buffer_size = 4096,
    },
    {
        .id = 2,
        .name = "control",
        .callback = callback_ws,
        .per_session_data_size = 128,
        .rx_buffer_size = 4096,
    },
    {
        0 /* terminator, .id = 0 */
    }
};

/* default mount serves the URL space from ./mount-origin */

static const struct lws_http_mount mount_opts = {
    /* .mount_next */       NULL,        /* linked-list "next" */
    /* .mountpoint */       "/",        /* mountpoint URL */
    /* .origin */           HTDOCS_DIR,   /* serve from dir */
    /* .def */              "index.html",   /* default filename */
    /* .protocol */         NULL,
    /* .cgienv */           NULL,
    /* .extra_mimetypes */      NULL,
    /* .interpret */        NULL,
    /* .cgi_timeout */      0,
    /* .cache_max_age */        0,
    /* .auth_mask */        0,
    /* .cache_reusable */       0,
    /* .cache_revalidate */     0,
    /* .cache_intermediaries */ 0,
    /* .origin_protocol */      LWSMPRO_FILE,   /* files in a dir */
    /* .mountpoint_len */       1,      /* char count */
    /* .basic_auth_login_file */    NULL,
    /* __dummy */ { 0 },
};

static void web_status_json(char **status_string_ptr, longmynd_status_t *status, longmynd_status_t *status_cache)
{
    JsonNode *statusObj;
    JsonNode *statusPacketObj;
    JsonNode *statusPacketRxObj;
    JsonNode *statusPacketTsObj;

    pthread_mutex_lock(&status->mutex);
    memcpy(status_cache, status, sizeof(longmynd_status_t));
    pthread_mutex_unlock(&status->mutex);

    statusObj = json_mkobject();

    json_append_member(statusObj, "type", json_mkstring("status"));
    json_append_member(statusObj, "timestamp", json_mknumber(((double)timestamp_ms())/1000));

    statusPacketObj = json_mkobject();

    /* Receiver */

    statusPacketRxObj = json_mkobject();

    json_append_member(statusPacketRxObj, "demod_state", json_mknumber((double)status_cache->state));

    json_append_member(statusPacketRxObj, "frequency", json_mknumber((double)(status_cache->frequency_requested+(status->frequency_offset/1000))));

    json_append_member(statusPacketRxObj, "lnb_voltage_enabled", json_mkbool(status_cache->polarisation_supply));

    json_append_member(statusPacketRxObj, "lnb_voltage_polarisation_h", json_mkbool(status_cache->polarisation_horizontal));

    json_append_member(statusPacketRxObj, "symbolrate", json_mknumber((double)status_cache->symbolrate));

    json_append_member(statusPacketRxObj, "vber", json_mknumber((double)status_cache->viterbi_error_rate));

    json_append_member(statusPacketRxObj, "ber", json_mknumber((double)status_cache->bit_error_rate));

    json_append_member(statusPacketRxObj, "errors_bch_uncorrected", json_mkbool(status_cache->errors_bch_uncorrected));

    json_append_member(statusPacketRxObj, "errors_bch_count", json_mknumber((double)status_cache->errors_bch_count));

    json_append_member(statusPacketRxObj, "errors_ldpc_count", json_mknumber((double)status_cache->errors_ldpc_count));

    json_append_member(statusPacketRxObj, "ber", json_mknumber((double)status_cache->bit_error_rate));

    json_append_member(statusPacketRxObj, "mer", json_mknumber((double)status_cache->modulation_error_rate));

    json_append_member(statusPacketRxObj, "modcod", json_mknumber((double)(status_cache->modcod)));

    json_append_member(statusPacketRxObj, "short_frame", json_mkbool(status_cache->short_frame));

    json_append_member(statusPacketRxObj, "pilot_symbols", json_mkbool(status_cache->pilots));

    JsonNode *constellationArray = json_mkarray();
    JsonNode *constellationPoint;
    for(int j=0; j<NUM_CONSTELLATIONS; j++)
    {
        /* Create point [x,y] */
        constellationPoint = json_mkarray();
        json_append_element(constellationPoint, json_mknumber(status_cache->constellation[j][0]));
        json_append_element(constellationPoint, json_mknumber(status_cache->constellation[j][1]));
        /* Add to array [[x,y],[x,y]] */
        json_append_element(constellationArray, constellationPoint);
    }
    json_append_member(statusPacketRxObj, "constellation", constellationArray);

    json_append_member(statusPacketObj, "rx", statusPacketRxObj);

    /* Transport Stream */

    statusPacketTsObj = json_mkobject();

    json_append_member(statusPacketTsObj, "service_name", json_mkstring(status_cache->service_name));

    json_append_member(statusPacketTsObj, "service_provider_name", json_mkstring(status_cache->service_provider_name));

    json_append_member(statusPacketTsObj, "null_ratio", json_mknumber((double)status_cache->ts_null_percentage));

    JsonNode *elementaryPIDsArray = json_mkarray();
    JsonNode *elementaryPID;
    for (int j=0; j<NUM_ELEMENT_STREAMS; j++) {
        if(status_cache->ts_elementary_streams[j][0] > 0)
        {
            elementaryPID = json_mkarray();
            json_append_element(elementaryPID, json_mknumber(status_cache->ts_elementary_streams[j][0]));
            json_append_element(elementaryPID, json_mknumber(status_cache->ts_elementary_streams[j][1]));
            /* Add to array [[x,y],[x,y]] */
            json_append_element(elementaryPIDsArray, elementaryPID);
        }
    }
    json_append_member(statusPacketTsObj, "PIDs", elementaryPIDsArray);

    json_append_member(statusPacketObj, "ts", statusPacketTsObj);

    json_append_member(statusObj, "packet", statusPacketObj);

    *status_string_ptr = json_stringify(statusObj, NULL);

    json_delete(statusObj);
}

/* Websocket Service Thread */
static struct lws_context *context;
static pthread_t ws_service_thread;
void *ws_service(void *arg)
{
    thread_vars_t *thread_vars = (thread_vars_t *)arg;
    uint8_t *err = &thread_vars->thread_err;
    int lws_err = 0;

    while (*err == ERROR_NONE && *thread_vars->main_err_ptr == ERROR_NONE)
    {
        lws_err = lws_service(context, 0);
        if(lws_err < 0)
        {
            fprintf(stderr, "Web: lws_service() reported error: %d\n", lws_err);
            *err = ERROR_WEB_LWS;
        }
    }

    return NULL;
}

void *loop_web(void *arg)
{
    thread_vars_t *thread_vars = (thread_vars_t *)arg;
    uint8_t *err = &thread_vars->thread_err;
    longmynd_config_t *config = thread_vars->config;
    longmynd_status_t *status_ptr = thread_vars->status;

    int status_json_length;
    char *status_json_str;
    static longmynd_status_t status_cache;

    struct lws_context_creation_info info;
    int logs = LLL_USER | LLL_ERR | LLL_WARN; // | LLL_NOTICE;

    lws_set_log_level(logs, NULL);

    memset(&info, 0, sizeof info);

    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8 | LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
    context = lws_create_context(&info); 
    if (!context)
    {
        lwsl_err("LWS: Init failed.\n");
        return NULL;
    }

    info.vhost_name = "localhost";
    info.port = config->web_port;
    info.mounts = &mount_opts;
    info.error_document_404 = "/404.html";
    info.protocols = protocols;

    if(!lws_create_vhost(context, &info))
    {
        lwsl_err("LWS: Failed to create vhost\n");
        lws_context_destroy(context);
        return NULL;
    }

    /* Create dedicated ws server thread */
    if(0 != pthread_create(&ws_service_thread, NULL, ws_service, (void *)thread_vars))
    {
        fprintf(stderr, "Error creating web_lws pthread\n");
    }
    else
    {
        pthread_setname_np(ws_service_thread, "Web - LWS");
    }

    uint64_t last_status_sent_monotonic = 0;
    while (*err == ERROR_NONE && *thread_vars->main_err_ptr == ERROR_NONE)
    {
        if(status_ptr->last_updated_monotonic != last_status_sent_monotonic)
        {
            last_status_sent_monotonic = status_ptr->last_updated_monotonic;
            web_status_json(&status_json_str, status_ptr, &status_cache);
            status_json_length = strlen(status_json_str);

            pthread_mutex_lock(&ws_monitor_output.mutex);
            memcpy(&ws_monitor_output.buffer[LWS_PRE], status_json_str, status_json_length);
            ws_monitor_output.length = status_json_length;
            ws_monitor_output.sequence_id++;
            pthread_mutex_unlock(&ws_monitor_output.mutex);

            lws_callback_on_writable_all_protocol(context, &protocols[WS_MONITOR]);
        }

        sleep_ms(10);
    }

    if(*err == ERROR_NONE)
    {
        /* Interrupt service thread */
        lws_cancel_service(context);
    }
    pthread_join(ws_service_thread, NULL);

    lws_context_destroy(context);

    return NULL;
}
