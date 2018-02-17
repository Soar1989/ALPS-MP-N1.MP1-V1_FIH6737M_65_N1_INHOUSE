/******************************************************************************
 *
 *  Copyright (C) 2003-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/******************************************************************************
 *
 *  This AVCTP module interfaces to L2CAP
 *
 ******************************************************************************/

#include <string.h>
#include "bt_types.h"
#include "bt_target.h"
#include "bt_utils.h"
#include "avct_api.h"
#include "avct_int.h"
#include "l2c_api.h"
#include "l2cdefs.h"

/* Configuration flags. */
#define AVCT_L2C_CFG_IND_DONE   (1<<0)
#define AVCT_L2C_CFG_CFM_DONE   (1<<1)

/* callback function declarations */
void avct_l2c_connect_ind_cback(BD_ADDR bd_addr, UINT16 lcid, UINT16 psm, UINT8 id);
void avct_l2c_connect_cfm_cback(UINT16 lcid, UINT16 result);
void avct_l2c_config_cfm_cback(UINT16 lcid, tL2CAP_CFG_INFO *p_cfg);
void avct_l2c_config_ind_cback(UINT16 lcid, tL2CAP_CFG_INFO *p_cfg);
void avct_l2c_disconnect_ind_cback(UINT16 lcid, BOOLEAN ack_needed);
void avct_l2c_disconnect_cfm_cback(UINT16 lcid, UINT16 result);
void avct_l2c_congestion_ind_cback(UINT16 lcid, BOOLEAN is_congested);
void avct_l2c_data_ind_cback(UINT16 lcid, BT_HDR *p_buf);

/* L2CAP callback function structure */
const tL2CAP_APPL_INFO avct_l2c_appl = {
    avct_l2c_connect_ind_cback,
    avct_l2c_connect_cfm_cback,
    NULL,
    avct_l2c_config_ind_cback,
    avct_l2c_config_cfm_cback,
    avct_l2c_disconnect_ind_cback,
    avct_l2c_disconnect_cfm_cback,
    NULL,
    avct_l2c_data_ind_cback,
    avct_l2c_congestion_ind_cback,
    NULL                                /* tL2CA_TX_COMPLETE_CB */
};

#if (MTK_AVCT_BROWSE_INCLUDED == TRUE)
/* callback function of browsing channel */
void avct_l2c_br_connect_ind_cback(BD_ADDR bd_addr, UINT16 lcid, UINT16 psm, UINT8 id);
void avct_l2c_br_connect_cfm_cback(UINT16 lcid, UINT16 result);
void avct_l2c_br_config_cfm_cback(UINT16 lcid, tL2CAP_CFG_INFO *p_cfg);
void avct_l2c_br_config_ind_cback(UINT16 lcid, tL2CAP_CFG_INFO *p_cfg);
void avct_l2c_br_disconnect_ind_cback(UINT16 lcid, BOOLEAN ack_needed);
void avct_l2c_br_disconnect_cfm_cback(UINT16 lcid, UINT16 result);
void avct_l2c_br_congestion_ind_cback(UINT16 lcid, BOOLEAN is_congested);
void avct_l2c_br_data_ind_cback(UINT16 lcid, BT_HDR *p_buf);

/* L2CAP callback function for browsing channel structure */
const tL2CAP_APPL_INFO avct_l2c_br_appl = {
    avct_l2c_br_connect_ind_cback,
    avct_l2c_br_connect_cfm_cback,
    NULL,
    avct_l2c_br_config_ind_cback,
    avct_l2c_br_config_cfm_cback,
    avct_l2c_br_disconnect_ind_cback,
    avct_l2c_br_disconnect_cfm_cback,
    NULL,
    avct_l2c_br_data_ind_cback,
    avct_l2c_br_congestion_ind_cback,
    NULL
};

/* L2CAP Enhance Retransmission config struct for browsing */
const tL2CAP_FCR_OPTS avct_br_l2c_fcr_opts_def =
{
    L2CAP_FCR_ERTM_MODE,                /* Mandatory for AVRCP browsing */
    MTK_AVCT_BR_FCR_OPT_TX_WINDOW_SIZE,     /* Tx window size */
    MTK_AVCT_BR_FCR_OPT_MAX_TX_B4_DISCNT,   /* Maximum transmissions before disconnecting */
    MTK_AVCT_BR_FCR_OPT_RETX_TOUT,          /* Retransmission timeout (2 secs) */
    MTK_AVCT_BR_FCR_OPT_MONITOR_TOUT,       /* Monitor timeout (12 secs) */
    MTK_AVCT_BR_FCR_OPT_MPS_SIZE            /* MPS segment size */
};
#endif

/*******************************************************************************
**
** Function         avct_l2c_is_passive
**
** Description      check is the CCB associated with the given LCB was created
**                  as passive
**
** Returns          TRUE, if the given LCB is created as AVCT_PASSIVE
**
*******************************************************************************/
static BOOLEAN avct_l2c_is_passive (tAVCT_LCB *p_lcb)
{
    BOOLEAN     is_passive = FALSE;
    tAVCT_CCB   *p_ccb = &avct_cb.ccb[0];
    int         i;

    for (i = 0; i < AVCT_NUM_CONN; i++, p_ccb++)
    {
        if (p_ccb->allocated && (p_ccb->p_lcb == p_lcb))
        {
            AVCT_TRACE_DEBUG("avct_l2c_is_ct control:x%x", p_ccb->cc.control);
            if (p_ccb->cc.control & AVCT_PASSIVE)
            {
                is_passive = TRUE;
                break;
            }
        }
    }
    return is_passive;
}

/*******************************************************************************
**
** Function         avct_l2c_connect_ind_cback
**
** Description      This is the L2CAP connect indication callback function.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_connect_ind_cback(BD_ADDR bd_addr, UINT16 lcid, UINT16 psm, UINT8 id)
{
    tAVCT_LCB       *p_lcb;
    UINT16          result = L2CAP_CONN_OK;
    tL2CAP_CFG_INFO cfg;
    UNUSED(psm);

    /* do we already have a channel for this peer? */
    if ((p_lcb = avct_lcb_by_bd(bd_addr)) == NULL)
    {
        /* no, allocate lcb */
        if ((p_lcb = avct_lcb_alloc(bd_addr)) == NULL)
        {
            /* no ccb available, reject L2CAP connection */
            result = L2CAP_CONN_NO_RESOURCES;
        }
    }
    /* else we already have a channel for this peer */
    else
    {
        if (!avct_l2c_is_passive (p_lcb) || (p_lcb->ch_state == AVCT_CH_OPEN))
        {
            /* this LCB included CT role - reject */
            result = L2CAP_CONN_NO_RESOURCES;
        }
        else
        {
            /* TG role only - accept the connection from CT. move the channel ID to the conflict list */
            p_lcb->conflict_lcid = p_lcb->ch_lcid;
            AVCT_TRACE_DEBUG("avct_l2c_connect_ind_cback conflict_lcid:0x%x", p_lcb->conflict_lcid);
        }
    }

    if(p_lcb)
    {
        AVCT_TRACE_DEBUG("avct_l2c_connect_ind_cback: 0x%x, res: %d, ch_state: %d",
            lcid, result, p_lcb->ch_state);
    }
    /* Send L2CAP connect rsp */
    L2CA_ConnectRsp(bd_addr, id, lcid, result, 0);

    /* if result ok, proceed with connection */
    if (result == L2CAP_CONN_OK)
    {
        /* store LCID */
        p_lcb->ch_lcid = lcid;

        /* transition to configuration state */
        p_lcb->ch_state = AVCT_CH_CFG;

        /* Send L2CAP config req */
        memset(&cfg, 0, sizeof(tL2CAP_CFG_INFO));
        cfg.mtu_present = TRUE;
        cfg.mtu = avct_cb.mtu;
        L2CA_ConfigReq(lcid, &cfg);
        AVCT_TRACE_DEBUG("avct_l2c snd Cfg Req");
    }

#if (BT_USE_TRACES == TRUE)
    if(p_lcb)
        AVCT_TRACE_DEBUG("ch_state cni: %d ", p_lcb->ch_state);
#endif
}

/*******************************************************************************
**
** Function         avct_l2c_connect_cfm_cback
**
** Description      This is the L2CAP connect confirm callback function.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_connect_cfm_cback(UINT16 lcid, UINT16 result)
{
    tAVCT_LCB       *p_lcb;
    tL2CAP_CFG_INFO cfg;

    /* look up lcb for this channel */
    if ((p_lcb = avct_lcb_by_lcid(lcid)) != NULL)
    {
        AVCT_TRACE_DEBUG("avct_l2c_connect_cfm_cback lcid:0x%x result: %d ch_state: %d, conflict_lcid:0x%x",
            lcid, result, p_lcb->ch_state, p_lcb->conflict_lcid);
        /* if in correct state */
        if (p_lcb->ch_state == AVCT_CH_CONN)
        {
            /* if result successful */
            if (result == L2CAP_CONN_OK)
            {
                /* set channel state */
                p_lcb->ch_state = AVCT_CH_CFG;

                /* Send L2CAP config req */
                memset(&cfg, 0, sizeof(tL2CAP_CFG_INFO));
                cfg.mtu_present = TRUE;
                cfg.mtu = avct_cb.mtu;
                L2CA_ConfigReq(lcid, &cfg);
                AVCT_TRACE_DEBUG("avct_l2c snd Cfg Req");
            }
            /* else failure */
            else
            {
                AVCT_TRACE_DEBUG("avct_l2c_connect_cfm_cback conflict_lcid:0x%x", p_lcb->conflict_lcid);
                if (p_lcb->conflict_lcid == lcid)
                    p_lcb->conflict_lcid = 0;
                else
                    avct_lcb_event(p_lcb, AVCT_LCB_LL_CLOSE_EVT, (tAVCT_LCB_EVT *) &result);
            }
        }
        else if (p_lcb->conflict_lcid == lcid)
        {
            /* we must be in AVCT_CH_CFG state for the ch_lcid channel */
            AVCT_TRACE_DEBUG("avct_l2c_connect_cfm_cback ch_state: %d, conflict_lcid:0x%x", p_lcb->ch_state, p_lcb->conflict_lcid);
            if (result == L2CAP_CONN_OK)
            {
                /* just in case the peer also accepts our connection - Send L2CAP disconnect req */
                L2CA_DisconnectReq(lcid);
            }
            p_lcb->conflict_lcid = 0;
        }
        AVCT_TRACE_DEBUG("ch_state cnc: %d ", p_lcb->ch_state);
    }
}

/*******************************************************************************
**
** Function         avct_l2c_config_cfm_cback
**
** Description      This is the L2CAP config confirm callback function.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_config_cfm_cback(UINT16 lcid, tL2CAP_CFG_INFO *p_cfg)
{
    tAVCT_LCB       *p_lcb;

    /* look up lcb for this channel */
    if ((p_lcb = avct_lcb_by_lcid(lcid)) != NULL)
    {
        AVCT_TRACE_DEBUG("avct_l2c_config_cfm_cback: 0x%x, ch_state: %d, res: %d",
            lcid, p_lcb->ch_state, p_cfg->result);
        /* if in correct state */
        if (p_lcb->ch_state == AVCT_CH_CFG)
        {
            /* if result successful */
            if (p_cfg->result == L2CAP_CFG_OK)
            {
                /* update flags */
                p_lcb->ch_flags |= AVCT_L2C_CFG_CFM_DONE;

                /* if configuration complete */
                if (p_lcb->ch_flags & AVCT_L2C_CFG_IND_DONE)
                {
                    p_lcb->ch_state = AVCT_CH_OPEN;
                    avct_lcb_event(p_lcb, AVCT_LCB_LL_OPEN_EVT, NULL);
                }
            }
            /* else failure */
            else
            {
                AVCT_TRACE_DEBUG("ERROR avct_l2c_config_cfm_cback L2CA_DisconnectReq %d ", p_lcb->ch_state);
                /* store result value */
                p_lcb->ch_result = p_cfg->result;

                /* Send L2CAP disconnect req */
                L2CA_DisconnectReq(lcid);
            }
        }
        AVCT_TRACE_DEBUG("ch_state cfc: %d ", p_lcb->ch_state);
    }
}

/*******************************************************************************
**
** Function         avct_l2c_config_ind_cback
**
** Description      This is the L2CAP config indication callback function.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_config_ind_cback(UINT16 lcid, tL2CAP_CFG_INFO *p_cfg)
{
    tAVCT_LCB       *p_lcb;

    /* look up lcb for this channel */
    if ((p_lcb = avct_lcb_by_lcid(lcid)) != NULL)
    {
        AVCT_TRACE_DEBUG("avct_l2c_config_ind_cback: 0x%x, ch_state: %d", lcid, p_lcb->ch_state);
        /* store the mtu in tbl */
        if (p_cfg->mtu_present)
        {
            p_lcb->peer_mtu = p_cfg->mtu;
        }
        else
        {
            p_lcb->peer_mtu = L2CAP_DEFAULT_MTU;
        }

        /* send L2CAP configure response */
        memset(p_cfg, 0, sizeof(tL2CAP_CFG_INFO));
        p_cfg->result = L2CAP_CFG_OK;
        L2CA_ConfigRsp(lcid, p_cfg);

        /* if first config ind */
        if ((p_lcb->ch_flags & AVCT_L2C_CFG_IND_DONE) == 0)
        {
            /* update flags */
            p_lcb->ch_flags |= AVCT_L2C_CFG_IND_DONE;

            /* if configuration complete */
            if (p_lcb->ch_flags & AVCT_L2C_CFG_CFM_DONE)
            {
                p_lcb->ch_state = AVCT_CH_OPEN;
                avct_lcb_event(p_lcb, AVCT_LCB_LL_OPEN_EVT, NULL);
            }
        }
        AVCT_TRACE_DEBUG("ch_state cfi: %d ", p_lcb->ch_state);
    }
}

/*******************************************************************************
**
** Function         avct_l2c_disconnect_ind_cback
**
** Description      This is the L2CAP disconnect indication callback function.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_disconnect_ind_cback(UINT16 lcid, BOOLEAN ack_needed)
{
    tAVCT_LCB       *p_lcb;
    UINT16          result = AVCT_RESULT_FAIL;

    /* look up lcb for this channel */
    if ((p_lcb = avct_lcb_by_lcid(lcid)) != NULL)
    {
        AVCT_TRACE_DEBUG("avct_l2c_disconnect_ind_cback: 0x%x, ch_state: %d", lcid, p_lcb->ch_state);
        if (ack_needed)
        {
            /* send L2CAP disconnect response */
            L2CA_DisconnectRsp(lcid);
        }

        avct_lcb_event(p_lcb, AVCT_LCB_LL_CLOSE_EVT, (tAVCT_LCB_EVT *) &result);
        AVCT_TRACE_DEBUG("ch_state di: %d ", p_lcb->ch_state);
    }
}

/*******************************************************************************
**
** Function         avct_l2c_disconnect_cfm_cback
**
** Description      This is the L2CAP disconnect confirm callback function.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_disconnect_cfm_cback(UINT16 lcid, UINT16 result)
{
    tAVCT_LCB       *p_lcb;
    UINT16          res;

    /* look up lcb for this channel */
    if ((p_lcb = avct_lcb_by_lcid(lcid)) != NULL)
    {
        AVCT_TRACE_DEBUG("avct_l2c_disconnect_cfm_cback: 0x%x, ch_state: %d, res: %d",
            lcid, p_lcb->ch_state, result);
        /* result value may be previously stored */
        res = (p_lcb->ch_result != 0) ? p_lcb->ch_result : result;
        p_lcb->ch_result = 0;

        avct_lcb_event(p_lcb, AVCT_LCB_LL_CLOSE_EVT, (tAVCT_LCB_EVT *) &res);
        AVCT_TRACE_DEBUG("ch_state dc: %d ", p_lcb->ch_state);
    }
}

/*******************************************************************************
**
** Function         avct_l2c_congestion_ind_cback
**
** Description      This is the L2CAP congestion indication callback function.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_congestion_ind_cback(UINT16 lcid, BOOLEAN is_congested)
{
    tAVCT_LCB       *p_lcb;

    AVCT_TRACE_DEBUG("avct_l2c_congestion_ind_cback: 0x%x", lcid);
    /* look up lcb for this channel */
    if ((p_lcb = avct_lcb_by_lcid(lcid)) != NULL)
    {
        avct_lcb_event(p_lcb, AVCT_LCB_LL_CONG_EVT, (tAVCT_LCB_EVT *) &is_congested);
    }
}

/*******************************************************************************
**
** Function         avct_l2c_data_ind_cback
**
** Description      This is the L2CAP data indication callback function.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_data_ind_cback(UINT16 lcid, BT_HDR *p_buf)
{
    tAVCT_LCB       *p_lcb;

    AVCT_TRACE_DEBUG("avct_l2c_data_ind_cback: 0x%x", lcid);
    /* look up lcb for this channel */
    if ((p_lcb = avct_lcb_by_lcid(lcid)) != NULL)
    {
        avct_lcb_event(p_lcb, AVCT_LCB_LL_MSG_EVT, (tAVCT_LCB_EVT *) &p_buf);
    }
    else /* prevent buffer leak */
    {
        AVCT_TRACE_WARNING("ERROR -> avct_l2c_data_ind_cback drop buffer");
        osi_free(p_buf);
    }
}

#if (MTK_AVCT_BROWSE_INCLUDED == TRUE)
/*******************************************************************************
**
** Function          avct_l2c_br_connect_ind_cback
**
** Description      This is the L2CAP connect indication callback function for browsing.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_br_connect_ind_cback(BD_ADDR bd_addr, UINT16 lcid, UINT16 psm, UINT8 id)
{
    tAVCT_LCB       *p_lcb = NULL;
    tAVCT_BCB       *p_bcb = NULL;
    UINT16          result = L2CAP_CONN_OK;
    tL2CAP_CFG_INFO cfg;
    tL2CAP_ERTM_INFO ertm_cfg;
    int index = AVCT_NUM_LINKS;
    int i;
    UNUSED(psm);

    AVCT_TRACE_DEBUG("avct_l2c_br_connect_ind_cback: lcid: %x, psm: %x, id: %x", lcid, psm, id);

    /* check if we have a control channel?  & check if we already have a browse channel*/
    if ((p_lcb = avct_lcb_by_bd(bd_addr)) == NULL || avct_bcb_by_lcb(p_lcb) != NULL || (index = avct_lcb_to_idx(p_lcb)) >= AVCT_NUM_LINKS) {
        /* no control channel lcb, reject L2CAP connection or already have browsing channel for this bd_addr*/
        AVCT_TRACE_WARNING("no lcb or already have bcb or lcb index error for %x:%x:%x:%x:%x:%x",
            bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
        result = L2CAP_CONN_NO_RESOURCES;
    } else {
        AVCT_TRACE_DEBUG("avct_l2c_br_connect_ind_cback: lcb index: %d", index);
        p_bcb = &avct_cb.bcb[index];
        p_bcb->allocated = index+1;
        p_bcb->ch_lcid = lcid;
        p_bcb->tx_q = fixed_queue_new(SIZE_MAX);

        for (i = 0; i < AVCT_NUM_CONN; i++) {
            if (avct_cb.ccb[i].p_lcb == p_lcb) {
                avct_cb.ccb[i].allocated |= AVCT_ALOC_BCB;
                break;
            }
        }

        if (i == AVCT_NUM_CONN) {
            AVCT_TRACE_WARNING("avct_l2c_br_connect_ind_cback: binded lcb not found");
            result = L2CAP_CONN_NO_RESOURCES;
        } else {
            memset(&ertm_cfg, 0, sizeof(tL2CAP_ERTM_INFO));
            ertm_cfg.preferred_mode = L2CAP_FCR_ERTM_MODE;
            ertm_cfg.allowed_modes = L2CAP_FCR_CHAN_OPT_ERTM;
            ertm_cfg.user_rx_buf_size = MTK_AVCT_BR_USER_RX_BUFFER_SIZE;
            ertm_cfg.user_tx_buf_size = MTK_AVCT_BR_USER_TX_BUFFER_SIZE;
            ertm_cfg.fcr_rx_buf_size = MTK_AVCT_BR_FCR_RX_BUFFER_SIZE;
            ertm_cfg.fcr_tx_buf_size = MTK_AVCT_BR_FCR_TX_BUFFER_SIZE;
        }
    }

    AVCT_TRACE_DEBUG("avct_l2c_br_connect_ind_cback: 0x%x, res: %d", lcid, result);

    /* Send L2CAP connect rsp */
    L2CA_ErtmConnectRsp(bd_addr, id, lcid, result, 0, ((result == L2CAP_CONN_OK)?&ertm_cfg:NULL));

    /* if result ok, proceed with connection */
    if (result == L2CAP_CONN_OK) {
        p_bcb->ch_state = AVCT_CH_CFG;
        /* Send L2CAP config req */
        memset(&cfg, 0, sizeof(tL2CAP_CFG_INFO));
        cfg.mtu_present = TRUE;
        cfg.mtu = avct_cb.mtu_br;
        cfg.fcr_present = TRUE;
        memcpy(&cfg.fcr, &avct_br_l2c_fcr_opts_def, sizeof(tL2CAP_FCR_OPTS));
        L2CA_ConfigReq(lcid, &cfg);
        AVCT_TRACE_DEBUG("avct_l2c_br_connect_ind_cback: browse snd Cfg Req");
        AVCT_TRACE_DEBUG("avct_l2c_br_connect_ind_cback: ch_state: %d ", p_lcb->ch_state);
    }
}

/*******************************************************************************
**
** Function           avct_l2c_br_connect_cfm_cback
**
** Description      This is the L2CAP connect confirm callback function for browsing.
**
**
** Returns           void
**
*******************************************************************************/
void avct_l2c_br_connect_cfm_cback(UINT16 lcid, UINT16 result)
{
    tAVCT_BCB       *p_bcb = NULL;
    tL2CAP_CFG_INFO cfg;

    /* look up lcb for this channel */
    if ((p_bcb = avct_bcb_by_lcid(lcid)) != NULL) {
        AVCT_TRACE_DEBUG("avct_l2c_br_connect_cfm_cback lcid:0x%x result: %d ch_state: %d",
            lcid, result, p_bcb->ch_state);
        /* if in correct state */
        if (p_bcb->ch_state == AVCT_CH_CONN) {
            /* if result successful */
            if (result == L2CAP_CONN_OK) {
                /* set channel state */
                p_bcb->ch_state = AVCT_CH_CFG;

                /* Send L2CAP config req */
                memset(&cfg, 0, sizeof(tL2CAP_CFG_INFO));
                cfg.mtu_present = TRUE;
                cfg.mtu = avct_cb.mtu;
                cfg.fcr_present = TRUE;
                memcpy(&cfg.fcr, &avct_br_l2c_fcr_opts_def, sizeof(tL2CAP_FCR_OPTS));
                L2CA_ConfigReq(lcid, &cfg);
                AVCT_TRACE_DEBUG("avct_l2c_br_connect_cfm_cback: browse snd Cfg Req");
            } else {
                AVCT_TRACE_DEBUG("avct_l2c_connect_cfm_cback lcid:0x%x", p_bcb->ch_lcid);
                avct_bcb_event(p_bcb, AVCT_LCB_LL_CLOSE_EVT, (tAVCT_LCB_EVT *) &result);
            }
        }

        AVCT_TRACE_DEBUG("avct_l2c_connect_cfm_cback ch_state cnc: %d ", p_bcb->ch_state);
    } else {
        AVCT_TRACE_WARNING("avct_l2c_connect_cfm_cback: no bcb for this lcid: %d", lcid);
    }
}

/*******************************************************************************
**
** Function          avct_l2c_br_config_cfm_cback
**
** Description      This is the L2CAP config confirm callback function for browsing.
**
**
** Returns           void
**
*******************************************************************************/
void avct_l2c_br_config_cfm_cback(UINT16 lcid, tL2CAP_CFG_INFO *p_cfg)
{
    tAVCT_BCB       *p_bcb = NULL;

    /* look up bcb for this channel */
    if ((p_bcb = avct_bcb_by_lcid(lcid)) != NULL) {
        AVCT_TRACE_DEBUG("avct_l2c_br_config_cfm_cback: 0x%x, ch_state: %d, res: %d",
            lcid, p_bcb->ch_state, p_cfg->result);
        /* if in correct state */
        if (p_bcb->ch_state == AVCT_CH_CFG) {
            /* if result successful */
            if (p_cfg->result == L2CAP_CFG_OK) {
                /* update flags */
                p_bcb->ch_flags |= AVCT_L2C_CFG_CFM_DONE;

                /* if configuration complete */
                if (p_bcb->ch_flags & AVCT_L2C_CFG_IND_DONE) {
                    p_bcb->ch_state = AVCT_CH_OPEN;
                    AVCT_TRACE_DEBUG("avct_l2c_br_config_cfm_cback: all config done");
                    avct_bcb_event(p_bcb, AVCT_LCB_LL_OPEN_EVT, NULL);
                }
            } else {
                AVCT_TRACE_DEBUG("ERROR avct_l2c_br_config_cfm_cback: L2CA_DisconnectReq %d ", p_bcb->ch_state);
                /* store result value */
                p_bcb->ch_result = p_cfg->result;

                /* Send L2CAP disconnect req */
                L2CA_DisconnectReq(lcid);
            }
        }

        AVCT_TRACE_DEBUG("avct_l2c_br_config_cfm_cback: ch_state: %d ", p_bcb->ch_state);
    } else {
        AVCT_TRACE_WARNING("avct_l2c_br_config_cfm_cback: no bcb for this lcid: %d", lcid);
    }
}

/*******************************************************************************
**
** Function         avct_l2c_br_config_ind_cback
**
** Description      This is the L2CAP config indication callback function for browsing.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_br_config_ind_cback(UINT16 lcid, tL2CAP_CFG_INFO *p_cfg)
{
    tAVCT_BCB *p_bcb = NULL;

    if ((p_bcb = avct_bcb_by_lcid(lcid)) != NULL) {
        AVCT_TRACE_DEBUG("avct_l2c_br_config_ind_cback: 0x%x, ch_state: %d", lcid, p_bcb->ch_state);
        if (p_cfg->mtu_present == TRUE) {
            p_bcb->peer_mtu = p_cfg->mtu;
        } else {
            p_bcb->peer_mtu = L2CAP_DEFAULT_MTU;
        }

        /* send L2CAP configure response */
        memset(p_cfg, 0, sizeof(tL2CAP_CFG_INFO));
        p_cfg->result = L2CAP_CFG_OK;
        L2CA_ConfigRsp(lcid, p_cfg);

        /* if first config ind */
        if ((p_bcb->ch_flags & AVCT_L2C_CFG_IND_DONE) == 0) {
            /* update flags */
            p_bcb->ch_flags |= AVCT_L2C_CFG_IND_DONE;

            /* if configuration complete */
            if (p_bcb->ch_flags & AVCT_L2C_CFG_CFM_DONE) {
                p_bcb->ch_state = AVCT_CH_OPEN;
                AVCT_TRACE_DEBUG("avct_l2c_br_config_ind_cback: all config done");
                avct_bcb_event(p_bcb, AVCT_LCB_LL_OPEN_EVT, NULL);
            }
        }

        AVCT_TRACE_DEBUG("avct_l2c_br_config_ind_cback: ch_state: %d ", p_bcb->ch_state);
    } else {
        AVCT_TRACE_WARNING("avct_l2c_br_config_ind_cback: no bcb for this lcid: %d", lcid);
    }
}

/*******************************************************************************
**
** Function         avct_l2c_br_disconnect_ind_cback
**
** Description      This is the L2CAP disconnect indication callback function for browsing.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_br_disconnect_ind_cback(UINT16 lcid, BOOLEAN ack_needed)
{
    tAVCT_BCB       *p_bcb = NULL;
    UINT16              result = AVCT_RESULT_FAIL;

    /* look up lcb for this channel */
    if ((p_bcb = avct_bcb_by_lcid(lcid)) != NULL) {
        AVCT_TRACE_DEBUG("avct_l2c_br_disconnect_ind_cback: 0x%x, ch_state: %d, ack_needed: %d", lcid, p_bcb->ch_state, ack_needed);
        if (ack_needed) {
            /* send L2CAP disconnect response */
            L2CA_DisconnectRsp(lcid);
        }

        avct_bcb_event(p_bcb, AVCT_LCB_LL_CLOSE_EVT, (tAVCT_LCB_EVT *) &result);
        AVCT_TRACE_DEBUG("avct_l2c_br_disconnect_ind_cback: ch_state di: %d ", p_bcb->ch_state);
    } else {
        AVCT_TRACE_DEBUG("avct_l2c_br_disconnect_ind_cback: no bcb for this lcid: %d", lcid);
    }
}

/*******************************************************************************
**
** Function         avct_l2c_br_disconnect_cfm_cback
**
** Description      This is the L2CAP disconnect confirm callback function for browsing.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_br_disconnect_cfm_cback(UINT16 lcid, UINT16 result)
{
    tAVCT_BCB       *p_bcb = NULL;
    UINT16              res;

    /* look up lcb for this channel */
    if ((p_bcb = avct_bcb_by_lcid(lcid)) != NULL) {
        AVCT_TRACE_DEBUG("avct_l2c_br_disconnect_cfm_cback: 0x%x, ch_state: %d, res: %d",
            lcid, p_bcb->ch_state, result);
        /* result value may be previously stored */
        res = (p_bcb->ch_result != 0) ? p_bcb->ch_result : result;
        p_bcb->ch_result = 0;

        avct_bcb_event(p_bcb, AVCT_LCB_LL_CLOSE_EVT, (tAVCT_LCB_EVT *) &res);
        AVCT_TRACE_DEBUG("avct_l2c_br_disconnect_cfm_cback: ch_state dc: %d ", p_bcb->ch_state);
    } else {
        AVCT_TRACE_DEBUG("avct_l2c_br_disconnect_cfm_cback: no bcb for this lcid: %d", lcid);
    }
}

/*******************************************************************************
**
** Function         avct_l2c_br_congestion_ind_cback
**
** Description      This is the L2CAP congestion indication callback function for browsing.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_br_congestion_ind_cback(UINT16 lcid, BOOLEAN is_congested)
{
    tAVCT_BCB       *p_bcb = NULL;

    AVCT_TRACE_DEBUG("avct_l2c_br_congestion_ind_cback: 0x%x, is_congested: %d", lcid, is_congested);
    /* look up lcb for this channel */
    if ((p_bcb = avct_bcb_by_lcid(lcid)) != NULL) {
        avct_bcb_event(p_bcb, AVCT_LCB_LL_CONG_EVT, (tAVCT_LCB_EVT *) &is_congested);
    } else {
        AVCT_TRACE_DEBUG("avct_l2c_br_congestion_ind_cback: no bcb for this lcid: %d", lcid);
    }
}

/*******************************************************************************
**
** Function         avct_l2c_data_ind_cback
**
** Description      This is the L2CAP data indication callback function.
**
**
** Returns          void
**
*******************************************************************************/
void avct_l2c_br_data_ind_cback(UINT16 lcid, BT_HDR *p_buf)
{
    tAVCT_BCB       *p_bcb = NULL;

    AVCT_TRACE_DEBUG("avct_l2c_br_data_ind_cback: 0x%x", lcid);
    /* look up lcb for this channel */
    if ((p_bcb = avct_bcb_by_lcid(lcid)) != NULL) {
        avct_bcb_event(p_bcb, AVCT_LCB_LL_MSG_EVT, (tAVCT_LCB_EVT *) &p_buf);
    } else { /* prevent buffer leak */
        AVCT_TRACE_WARNING("ERROR -> avct_l2c_br_data_ind_cback drop buffer");
        osi_free(p_buf);
    }
}
#endif
