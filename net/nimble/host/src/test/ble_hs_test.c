/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "os/os.h"
#include "nimble/hci_common.h"
#include "ble_hs_priv.h"
#include "host/ble_hs_test.h"
#include "testutil/testutil.h"
#include "ble_l2cap.h"
#include "ble_hs_test_util.h"

void
ble_hs_test_pkt_txed(struct os_mbuf *om)
{
    os_mbuf_free_chain(ble_hs_test_util_prev_tx);
    ble_hs_test_util_prev_tx = om;

    /* XXX: For now, just strip the HCI ACL data and L2CAP headers. */
    os_mbuf_adj(om, BLE_HCI_DATA_HDR_SZ + BLE_L2CAP_HDR_SZ);
}

void
ble_hs_test_hci_txed(uint8_t *cmdbuf)
{
    if (ble_hs_test_util_prev_hci_tx != NULL) {
        os_memblock_put(&g_hci_cmd_pool, ble_hs_test_util_prev_hci_tx);
    }
    ble_hs_test_util_prev_hci_tx = cmdbuf;
}

#ifdef PKG_TEST

int
main(void)
{
    tu_config.tc_print_results = 1;
    tu_init();

    l2cap_test_all();
    ble_att_svr_test_all();
    ble_att_clt_test_all();
    ble_host_hci_test_all();
    ble_hs_conn_test_all();
    ble_os_test_all();
    ble_uuid_test_all();
    ble_gatt_disc_s_test_all();
    ble_gatt_disc_c_test_all();
    ble_gatt_read_test_all();
    ble_gatt_write_test_all();
    ble_gatt_conn_test_all();
    ble_hs_adv_test_all();

    return tu_any_failed;
}

#endif
