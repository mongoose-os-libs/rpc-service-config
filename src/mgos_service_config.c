/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mgos_service_config.h"
#include "mgos_rpc.h"

#include "common/mg_str.h"
#include "mgos_config_util.h"
#include "mgos_hal.h"
#include "mgos_sys_config.h"
#include "mgos_utils.h"

#if CS_PLATFORM == CS_P_ESP8266
#include "fw/platforms/esp8266/src/esp_gpio.h"
#endif

/* Handler for Config.Get */
static void mgos_config_get_handler(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  const struct mgos_conf_entry *schema = mgos_config_schema();
  struct mbuf send_mbuf;
  mbuf_init(&send_mbuf, 0);

  char *key = NULL;
  json_scanf(args.p, args.len, ri->args_fmt, &key);
  if (key != NULL) {
    schema = mgos_conf_find_schema_entry(key, mgos_config_schema());
    free(key);
    if (schema == NULL) {
      mg_rpc_send_errorf(ri, 404, "invalid config key");
      return;
    }
  }

  mgos_conf_emit_cb(&mgos_sys_config, NULL, schema, false, &send_mbuf, NULL,
                    NULL);

  /*
   * TODO(dfrank): figure out why frozen handles %.*s incorrectly here,
   * fix it, and remove this hack with adding NULL byte
   */
  mbuf_append(&send_mbuf, "", 1);
  mg_rpc_send_responsef(ri, "%s", send_mbuf.buf);
  ri = NULL;

  mbuf_free(&send_mbuf);

  (void) cb_arg;
  (void) args;
  (void) fi;
}

/*
 * Called by json_scanf() for the "config" field, and parses all the given
 * JSON as sys config
 */
static void set_handler(const char *str, int len, void *user_data) {
  mgos_config_apply_s(mg_mk_str_n(str, len), false);
  (void) user_data;
}

/* Handler for Config.Set */
static void mgos_config_set_handler(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  json_scanf(args.p, args.len, ri->args_fmt, set_handler, NULL);
  mg_rpc_send_responsef(ri, NULL);
  ri = NULL;
  (void) cb_arg;
  (void) fi;
}

/* Handler for Config.Save */
static void mgos_config_save_handler(struct mg_rpc_request_info *ri,
                                     void *cb_arg, struct mg_rpc_frame_info *fi,
                                     struct mg_str args) {
  /*
   * We need to stash mg_rpc pointer since we need to use it after calling
   * mg_rpc_send_responsef(), which invalidates `ri`
   */
  char *msg = NULL;
  int try_once = false, reboot = 0;

  json_scanf(args.p, args.len, ri->args_fmt, &try_once, &reboot);

  if (!mgos_sys_config_save(&mgos_sys_config, try_once, &msg)) {
    mg_rpc_send_errorf(ri, -1, "error saving config: %s", (msg ? msg : ""));
    ri = NULL;
    free(msg);
    return;
  }

#if CS_PLATFORM == CS_P_ESP8266
  if (reboot && esp_strapping_to_bootloader()) {
    /*
     * This is the first boot after flashing. If we reboot now, we're going to
     * the boot loader and it will appear as if the fw is not booting.
     * This is very confusing, so we ask the user to reboot.
     */
    mg_rpc_send_errorf(ri, 418,
                       "configuration has been saved but manual device reset "
                       "is required. For details, see https://goo.gl/Ja5gUv");
  } else
#endif
  {
    mg_rpc_send_responsef(ri, NULL);
  }
  ri = NULL;

  if (reboot) {
    mgos_system_restart_after(500);
  }

  (void) cb_arg;
  (void) fi;
}

bool mgos_rpc_service_config_init(void) {
  struct mg_rpc *c = mgos_rpc_get_global();
  mg_rpc_add_handler(c, "Config.Get", "{key: %Q}", mgos_config_get_handler,
                     NULL);
  mg_rpc_add_handler(c, "Config.Set", "{config: %M}", mgos_config_set_handler,
                     NULL);
  mg_rpc_add_handler(c, "Config.Save", "{try_once: %B, reboot: %B}",
                     mgos_config_save_handler, NULL);
  return true;
}
