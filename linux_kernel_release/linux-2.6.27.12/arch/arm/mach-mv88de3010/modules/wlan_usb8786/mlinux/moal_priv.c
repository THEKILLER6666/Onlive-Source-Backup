/** @file  moal_priv.c
  *
  * @brief This file contains standard ioctl functions
  *
  * Copyright (C) 2008-2009, Marvell International Ltd.
  * All Rights Reserved
  */

/************************************************************************
Change log:
    10/30/2008: initial version
************************************************************************/

#include	"moal_main.h"

/********************************************************
                Local Variables
********************************************************/
/** Bands supported in Infra mode */
static t_u8 SupportedInfraBand[] = {
    BAND_B, BAND_B | BAND_G, BAND_G,
    BAND_GN, BAND_B | BAND_G | BAND_GN, BAND_G | BAND_GN,
};

/** Bands supported in Ad-Hoc mode */
static t_u8 SupportedAdhocBand[] = {
    BAND_B, BAND_B | BAND_G, BAND_G,
};

/********************************************************
		Global Variables
********************************************************/
#ifdef DEBUG_LEVEL1
extern t_u32 drvdbg;
extern t_u32 ifdbg;
#endif

/********************************************************
		Local Functions
********************************************************/
/**
 *  @brief Return hex value of a give character
 *
 *  @param chr	    Character to be converted
 *
 *  @return 	    The converted chanrater if chr is a valid hex, else 0
 */
static int
woal_hexval(char chr)
{
    ENTER();

    if (chr >= '0' && chr <= '9')
        return chr - '0';
    if (chr >= 'A' && chr <= 'F')
        return chr - 'A' + 10;
    if (chr >= 'a' && chr <= 'f')
        return chr - 'a' + 10;

    LEAVE();
    return 0;
}

/**
 *  @brief Return hex value of a given ascii string
 *
 *  @param a	    String to be converted to ascii
 *
 *  @return 	    The converted chanrater if a is a valid hex, else 0
 */
static int
woal_atox(char *a)
{
    int i = 0;

    ENTER();

    while (isxdigit(*a))
        i = i * 16 + woal_hexval(*a++);

    LEAVE();
    return i;
}

/**
 *  @brief Get Driver Version
 *
 *  @param priv         A pointer to moal_private structure
 *  @param req          A pointer to ifreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_get_driver_version(moal_private * priv, struct ifreq *req)
{
    struct iwreq *wrq = (struct iwreq *) req;
    int len;
    char buf[128];
    ENTER();

    woal_get_version(priv->phandle, buf, sizeof(buf) - 1);

    len = strlen(buf);
    if (wrq->u.data.pointer) {
        if (copy_to_user(wrq->u.data.pointer, buf, len)) {
            PRINTM(ERROR, "Copy to user failed\n");
            LEAVE();
            return -EFAULT;
        }
        wrq->u.data.length = len;
    }
    PRINTM(INFO, "MOAL VERSION: %s\n", buf);
    LEAVE();
    return 0;
}

/**
 *  @brief Get extended driver version
 *
 *  @param priv         A pointer to moal_private structure
 *  @param ireq         A pointer to ifreq structure
 *
 *  @return             0 --success, otherwise fail  
 */
static int
woal_get_driver_verext(moal_private * priv, struct ifreq *ireq)
{
    struct iwreq *wrq = (struct iwreq *) ireq;
    mlan_ds_get_info *info = NULL;
    mlan_ioctl_req *req = NULL;

    ENTER();

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_get_info));
    if (req == NULL) {
        LEAVE();
        return -ENOMEM;
    }

    info = (mlan_ds_get_info *) req->pbuf;
    info->sub_command = MLAN_OID_GET_VER_EXT;
    req->req_id = MLAN_IOCTL_GET_INFO;
    req->action = MLAN_ACT_GET;

    if (!wrq->u.data.flags) {
        info->param.ver_ext.version_str_sel =
            *((int *) (wrq->u.name + SUBCMD_OFFSET));
    } else {
        if (copy_from_user
            (&info->param.ver_ext.version_str_sel, wrq->u.data.pointer,
             sizeof(info->param.ver_ext.version_str_sel))) {
            PRINTM(ERROR, "Copy from user failed\n");
            LEAVE();
            return -EFAULT;
        }
    }

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        LEAVE();
        return -EFAULT;
    }

    if (wrq->u.data.pointer) {
        if (copy_to_user(wrq->u.data.pointer, info->param.ver_ext.version_str,
                         strlen(info->param.ver_ext.version_str))) {
            PRINTM(ERROR, "Copy to user failed\n");
            LEAVE();
            return -EFAULT;
        }
        wrq->u.data.length = strlen(info->param.ver_ext.version_str);
    }

    PRINTM(INFO, "MOAL EXTENDED VERSION: %s\n",
           info->param.ver_ext.version_str);

    LEAVE();
    return 0;
}

/**
 *  @brief Performs warm reset
 *
 *  @param priv         A pointer to moal_private structure
 *
 *  @return             0 --success, otherwise fail  
 */
static int
woal_warm_reset(moal_private * priv)
{
    int ret = 0;
    int intf_num;
    moal_handle *handle = priv->phandle;
    mlan_ioctl_req *req = NULL;
    mlan_ds_misc_cfg *misc = NULL;
    mlan_ds_hs_cfg hscfg;
    mlan_ds_pm_cfg *pmcfg = NULL;

    ENTER();

    /* Disable interfaces */
    for (intf_num = handle->priv_num; intf_num >= 0; intf_num--) {
        netif_stop_queue(handle->priv[intf_num]->netdev);
        netif_device_detach(handle->priv[intf_num]->netdev);
    }

    memset(&hscfg, 0, sizeof(mlan_ds_hs_cfg));
    woal_get_hs_params(priv, MOAL_IOCTL_WAIT, &hscfg);
    if (hscfg.conditions != HOST_SLEEP_CFG_CANCEL) {
        req = woal_alloc_mlan_ioctl_req_no_wait(sizeof(mlan_ds_pm_cfg));
        if (req == NULL) {
            ret = MLAN_STATUS_FAILURE;
            goto done;
        }

        pmcfg = (mlan_ds_pm_cfg *) req->pbuf;
        pmcfg->sub_command = MLAN_OID_PM_CFG_HS_CFG;
        req->req_id = MLAN_IOCTL_PM_CFG;
        req->action = MLAN_ACT_SET;

        pmcfg->param.hs_cfg.conditions = HOST_SLEEP_CFG_CANCEL;
        pmcfg->param.hs_cfg.is_invoke_hostcmd = MTRUE;

        if (MLAN_STATUS_SUCCESS !=
            woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            kfree(req);
            goto done;
        }
        kfree(req);
    }

    /* Disconnect from network */
    for (intf_num = handle->priv_num; intf_num >= 0; intf_num--) {
        if (handle->priv[intf_num]->media_connected == MTRUE) {
            woal_disconnect(handle->priv[intf_num], NULL);
        }
    }

    /* Cancel heart beat timer */
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
    if (req) {
        misc = (mlan_ds_misc_cfg *) req->pbuf;
        misc->sub_command = MLAN_OID_MISC_HEART_BEAT;
        req->req_id = MLAN_IOCTL_MISC_CFG;
        req->action = MLAN_ACT_SET;
        misc->param.heart_beat.h2d_timer = HB_CFG_DISABLE;
        misc->param.heart_beat.d2h_timer = HB_CFG_DISABLE;
        if (MLAN_STATUS_SUCCESS !=
            woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            kfree(req);
            goto done;
        }
        kfree(req);
    }

    /* Initialize private structures */
    for (intf_num = handle->priv_num; intf_num >= 0; intf_num--)
        woal_init_priv(handle->priv[intf_num]);

#ifdef REASSOCIATION
    /* Reset the reassoc timer and status */
    handle->reassoc_on = MFALSE;
    if (handle->is_reassoc_timer_set == MTRUE) {
        woal_cancel_timer(&handle->reassoc_timer);
        handle->is_reassoc_timer_set = MFALSE;
    }
#endif

    /* Restart the firmware */
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
    if (req) {
        misc = (mlan_ds_misc_cfg *) req->pbuf;
        misc->sub_command = MLAN_OID_MISC_WARM_RESET;
        req->req_id = MLAN_IOCTL_MISC_CFG;
        req->action = MLAN_ACT_SET;
        if (MLAN_STATUS_SUCCESS !=
            woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            kfree(req);
            goto done;
        }
        kfree(req);
    }

    /* Enable interfaces */
    for (intf_num = handle->priv_num; intf_num >= 0; intf_num--) {
        netif_device_attach(handle->priv[intf_num]->netdev);
        netif_start_queue(handle->priv[intf_num]->netdev);
    }

  done:
    LEAVE();
    return ret;
}

/**
 *  @brief Get signal
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq	        A pointer to iwreq structure
 *
 *  @return 	  	0 --success, otherwise fail
 */
static int
woal_get_signal(moal_private * priv, struct iwreq *wrq)
{
/** Input data size */
#define IN_DATA_SIZE	2
/** Output data size */
#define OUT_DATA_SIZE	12
    int ret = 0;
    int in_data[IN_DATA_SIZE];
    int out_data[OUT_DATA_SIZE];
    mlan_ds_get_signal signal;
    int data_length = 0;

    ENTER();

    memset(in_data, 0, sizeof(in_data));
    memset(out_data, 0, sizeof(out_data));

    if (priv->media_connected == MFALSE) {
        PRINTM(ERROR, "Can not get RSSI in disconnected state\n");
        ret = -ENOTSUPP;
        goto done;
    }

    if (wrq->u.data.length) {
        if (copy_from_user
            (in_data, wrq->u.data.pointer,
             sizeof(int) * MIN(wrq->u.data.length, sizeof(in_data)))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
    }

    switch (wrq->u.data.length) {
    case 0:                    /* No checking, get everything */
        break;
    case 2:                    /* Check subtype range */
        if (in_data[1] < 1 || in_data[1] > 4) {
            ret = -EINVAL;
            goto done;
        }
        /* Fall through */
    case 1:                    /* Check type range */
        if (in_data[0] < 1 || in_data[0] > 3) {
            ret = -EINVAL;
            goto done;
        }
        break;
    default:
        ret = -EINVAL;
        goto done;
    }

    memset(&signal, 0, sizeof(mlan_ds_get_signal));
    if (MLAN_STATUS_SUCCESS !=
        woal_get_signal_info(priv, MOAL_IOCTL_WAIT, &signal)) {
        ret = -EFAULT;
        goto done;
    }
    PRINTM(INFO, "RSSI Beacon Last   : %d\n", (int) signal.bcn_rssi_last);
    PRINTM(INFO, "RSSI Beacon Average: %d\n", (int) signal.bcn_rssi_avg);
    PRINTM(INFO, "RSSI Data Last     : %d\n", (int) signal.data_rssi_last);
    PRINTM(INFO, "RSSI Data Average  : %d\n", (int) signal.data_rssi_avg);
    PRINTM(INFO, "SNR Beacon Last    : %d\n", (int) signal.bcn_snr_last);
    PRINTM(INFO, "SNR Beacon Average : %d\n", (int) signal.bcn_snr_avg);
    PRINTM(INFO, "SNR Data Last      : %d\n", (int) signal.data_snr_last);
    PRINTM(INFO, "SNR Data Average   : %d\n", (int) signal.data_snr_avg);
    PRINTM(INFO, "NF Beacon Last     : %d\n", (int) signal.bcn_nf_last);
    PRINTM(INFO, "NF Beacon Average  : %d\n", (int) signal.bcn_nf_avg);
    PRINTM(INFO, "NF Data Last       : %d\n", (int) signal.data_nf_last);
    PRINTM(INFO, "NF Data Average    : %d\n", (int) signal.data_nf_avg);

    /* Check type */
    switch (in_data[0]) {
    case 0:                    /* Send everything */
        out_data[data_length++] = signal.bcn_rssi_last;
        out_data[data_length++] = signal.bcn_rssi_avg;
        out_data[data_length++] = signal.data_rssi_last;
        out_data[data_length++] = signal.data_rssi_avg;
        out_data[data_length++] = signal.bcn_snr_last;
        out_data[data_length++] = signal.bcn_snr_avg;
        out_data[data_length++] = signal.data_snr_last;
        out_data[data_length++] = signal.data_snr_avg;
        out_data[data_length++] = signal.bcn_nf_last;
        out_data[data_length++] = signal.bcn_nf_avg;
        out_data[data_length++] = signal.data_nf_last;
        out_data[data_length++] = signal.data_nf_avg;
        break;
    case 1:                    /* RSSI */
        /* Check subtype */
        switch (in_data[1]) {
        case 0:                /* Everything */
            out_data[data_length++] = signal.bcn_rssi_last;
            out_data[data_length++] = signal.bcn_rssi_avg;
            out_data[data_length++] = signal.data_rssi_last;
            out_data[data_length++] = signal.data_rssi_avg;
            break;
        case 1:                /* bcn last */
            out_data[data_length++] = signal.bcn_rssi_last;
            break;
        case 2:                /* bcn avg */
            out_data[data_length++] = signal.bcn_rssi_avg;
            break;
        case 3:                /* data last */
            out_data[data_length++] = signal.data_rssi_last;
            break;
        case 4:                /* data avg */
            out_data[data_length++] = signal.data_rssi_avg;
            break;
        default:
            break;
        }
        break;
    case 2:                    /* SNR */
        /* Check subtype */
        switch (in_data[1]) {
        case 0:                /* Everything */
            out_data[data_length++] = signal.bcn_snr_last;
            out_data[data_length++] = signal.bcn_snr_avg;
            out_data[data_length++] = signal.data_snr_last;
            out_data[data_length++] = signal.data_snr_avg;
            break;
        case 1:                /* bcn last */
            out_data[data_length++] = signal.bcn_snr_last;
            break;
        case 2:                /* bcn avg */
            out_data[data_length++] = signal.bcn_snr_avg;
            break;
        case 3:                /* data last */
            out_data[data_length++] = signal.data_snr_last;
            break;
        case 4:                /* data avg */
            out_data[data_length++] = signal.data_snr_avg;
            break;
        default:
            break;
        }
        break;
    case 3:                    /* NF */
        /* Check subtype */
        switch (in_data[1]) {
        case 0:                /* Everything */
            out_data[data_length++] = signal.bcn_nf_last;
            out_data[data_length++] = signal.bcn_nf_avg;
            out_data[data_length++] = signal.data_nf_last;
            out_data[data_length++] = signal.data_nf_avg;
            break;
        case 1:                /* bcn last */
            out_data[data_length++] = signal.bcn_nf_last;
            break;
        case 2:                /* bcn avg */
            out_data[data_length++] = signal.bcn_nf_avg;
            break;
        case 3:                /* data last */
            out_data[data_length++] = signal.data_nf_last;
            break;
        case 4:                /* data avg */
            out_data[data_length++] = signal.data_nf_avg;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    wrq->u.data.length = data_length;
    if (copy_to_user(wrq->u.data.pointer, out_data,
                     wrq->u.data.length * sizeof(out_data[0]))) {
        PRINTM(ERROR, "Copy to user failed\n");
        ret = -EFAULT;
        goto done;
    }
  done:
    LEAVE();
    return ret;
}

/** Heart Beat Config Input: No change */
#define HB_NO_CHANGE         -1
/** Heart Beat Config Input: Debug */
#define HB_DEBUG             -2
/** Heart Beat Config Input: Maximum value */
#define HB_MAX_VALUE         0xFFFE
/** Heart Beat Config Input: Minimum value */
#define HB_MIN_VALUE         10

/**
 *  @brief Enable/disable Heart beat feature
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq	        A pointer to iwreq structure
 *
 *  @return 	  	0 --success, otherwise fail
 */
static int
woal_heart_beat_ioctl(moal_private * priv, struct iwreq *wrq)
{
    int user_data_len;
    int data[2];
    int ret = 0;
    mlan_ioctl_req *req = NULL;
    mlan_ds_misc_cfg *misc = NULL;

    ENTER();

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
    if (req == NULL) {
        LEAVE();
        return -ENOMEM;
    }
    misc = (mlan_ds_misc_cfg *) req->pbuf;
    misc->sub_command = MLAN_OID_MISC_HEART_BEAT;
    req->req_id = MLAN_IOCTL_MISC_CFG;

    user_data_len = wrq->u.data.length;
    if (!user_data_len) {
        req->action = MLAN_ACT_GET;
    } else if (user_data_len == 2) {
        if (copy_from_user
            (data, wrq->u.data.pointer, sizeof(int) * user_data_len)) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        if (data[0] < HB_DEBUG || data[0] > HB_MAX_VALUE ||
            data[1] < HB_DEBUG || data[1] > HB_MAX_VALUE) {
            ret = -EINVAL;
            goto done;
        }
        if ((data[0] > 0 && data[0] < HB_MIN_VALUE) ||
            (data[1] > 0 && data[1] < HB_MIN_VALUE)) {
            PRINTM(ERROR,
                   "The minimum time interval of heart beat is 10 (1s)\n");
            ret = -EINVAL;
            goto done;
        }

        req->action = MLAN_ACT_SET;

        misc->param.heart_beat.h2d_timer = data[0];
        if (!data[0])
            misc->param.heart_beat.h2d_timer = HB_CFG_DISABLE;
        else if (data[0] == HB_NO_CHANGE || data[0] == HB_DEBUG)
            misc->param.heart_beat.h2d_timer = HB_CFG_NO_CHANGE;
        if (data[0] == HB_DEBUG)
            misc->param.heart_beat.debug_for_host = MTRUE;
        misc->param.heart_beat.d2h_timer = data[1];
        if (!data[1] || data[1] == HB_DEBUG)
            misc->param.heart_beat.d2h_timer = HB_CFG_DISABLE;
        else if (data[1] == HB_NO_CHANGE)
            misc->param.heart_beat.d2h_timer = HB_CFG_NO_CHANGE;
        if (data[1] == HB_DEBUG)
            misc->param.heart_beat.debug_for_device = MTRUE;
    } else {
        PRINTM(ERROR, "Invalid number of args!\n");
        ret = -EINVAL;
        goto done;
    }

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    data[0] = misc->param.heart_beat.h2d_timer;
    data[1] = misc->param.heart_beat.d2h_timer;
    wrq->u.data.length = 2;
    if (copy_to_user
        (wrq->u.data.pointer, data, sizeof(int) * wrq->u.data.length)) {
        PRINTM(ERROR, "Copy to user failed\n");
        ret = -EFAULT;
        goto done;
    }

  done:
    if (req)
        kfree(req);

    LEAVE();
    return ret;
}

/**
 *  @brief Set/Get Usr 11n configuration request
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq	    A pointer to iwreq structure
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_11n_htcap_cfg(moal_private * priv, struct iwreq *wrq)
{
    int data;
    mlan_ioctl_req *req = NULL;
    mlan_ds_11n_cfg *cfg_11n = NULL;
    int ret = 0;

    ENTER();

    if (wrq->u.data.length > 1) {
        PRINTM(ERROR, "Invalid number of arguments\n");
        ret = -EINVAL;
        goto done;
    }

    if (((req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg))) == NULL)) {
        ret = -ENOMEM;
        goto done;
    }

    cfg_11n = (mlan_ds_11n_cfg *) req->pbuf;
    cfg_11n->sub_command = MLAN_OID_11N_HTCAP_CFG;
    req->req_id = MLAN_IOCTL_11N_CFG;

    if (wrq->u.data.length == 0) {
        /* Get 11n tx parameters from MLAN */
        req->action = MLAN_ACT_GET;
    } else if (wrq->u.data.length == 1) {
        if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }

        cfg_11n->param.htcap_cfg = data;
        PRINTM(INFO, "SET: htusrcap:0x%x\n", data);
        /* Update 11n tx parameters in MLAN */
        req->action = MLAN_ACT_SET;
    }

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    data = cfg_11n->param.htcap_cfg;
    PRINTM(INFO, "GET: httxcap:0x%x\n", data);

    if (copy_to_user(wrq->u.data.pointer, &data, sizeof(data))) {
        PRINTM(ERROR, "Copy to user failed\n");
        ret = -EFAULT;
        goto done;
    }

    wrq->u.data.length = 1;

  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Enable/Disable amsdu_aggr_ctrl
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq	    A pointer to iwreq structure
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_11n_amsdu_aggr_ctrl(moal_private * priv, struct iwreq *wrq)
{
    int data[2];
    mlan_ioctl_req *req = NULL;
    mlan_ds_11n_cfg *cfg_11n = NULL;
    int ret = 0;

    ENTER();

    if ((wrq->u.data.length != 0) && (wrq->u.data.length != 1)) {
        PRINTM(ERROR, "Invalid number of arguments\n");
        ret = -EINVAL;
        goto done;
    }
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    cfg_11n = (mlan_ds_11n_cfg *) req->pbuf;
    cfg_11n->sub_command = MLAN_OID_11N_CFG_AMSDU_AGGR_CTRL;
    req->req_id = MLAN_IOCTL_11N_CFG;

    if (wrq->u.data.length == 0) {
        /* Get 11n tx parameters from MLAN */
        req->action = MLAN_ACT_GET;
    } else if (wrq->u.data.length == 1) {
        if (copy_from_user(data, wrq->u.data.pointer,
                           wrq->u.data.length * sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        cfg_11n->param.amsdu_aggr_ctrl.enable = data[0];
        /* Update 11n tx parameters in MLAN */
        req->action = MLAN_ACT_SET;
    }
    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }
    data[0] = cfg_11n->param.amsdu_aggr_ctrl.enable;
    data[1] = cfg_11n->param.amsdu_aggr_ctrl.curr_buf_size;

    if (copy_to_user(wrq->u.data.pointer, data, sizeof(data))) {
        PRINTM(ERROR, "Copy to user failed\n");
        ret = -EFAULT;
        goto done;
    }
    wrq->u.data.length = 2;
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Set/Get 11n configuration request
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq	    A pointer to iwreq structure
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_11n_tx_cfg(moal_private * priv, struct iwreq *wrq)
{
    int data;
    mlan_ioctl_req *req = NULL;
    mlan_ds_11n_cfg *cfg_11n = NULL;
    int ret = 0;

    ENTER();

    if ((wrq->u.data.length != 0) && (wrq->u.data.length != 1)) {
        PRINTM(ERROR, "Invalid number of arguments\n");
        ret = -EINVAL;
        goto done;
    }
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    cfg_11n = (mlan_ds_11n_cfg *) req->pbuf;
    cfg_11n->sub_command = MLAN_OID_11N_CFG_TX;
    req->req_id = MLAN_IOCTL_11N_CFG;

    if (wrq->u.data.length == 0) {
        /* Get 11n tx parameters from MLAN */
        req->action = MLAN_ACT_GET;
    } else if (wrq->u.data.length == 1) {
        if (copy_from_user(&data, wrq->u.data.pointer, sizeof(data))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        cfg_11n->param.tx_cfg.httxcap = data;
        PRINTM(INFO, "SET: httxcap:%d\n", data);
        /* Update 11n tx parameters in MLAN */
        req->action = MLAN_ACT_SET;
    }
    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }
    data = cfg_11n->param.tx_cfg.httxcap;
    PRINTM(INFO, "GET: httxcap:%d\n", data);
    if (copy_to_user(wrq->u.data.pointer, &data, sizeof(data))) {
        PRINTM(ERROR, "Copy to user failed\n");
        ret = -EFAULT;
        goto done;
    }
    wrq->u.data.length = 1;
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Enable/Disable TX Aggregation
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq	    A pointer to iwreq structure
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_11n_prio_tbl(moal_private * priv, struct iwreq *wrq)
{
    int data[MAX_NUM_TID * 2], i, j;
    mlan_ioctl_req *req = NULL;
    mlan_ds_11n_cfg *cfg_11n = NULL;
    int ret = 0;

    ENTER();

    if ((wrq->u.data.pointer == NULL)) {
        LEAVE();
        return -EINVAL;
    }

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
    if (req == NULL) {
        LEAVE();
        return -ENOMEM;
    }
    cfg_11n = (mlan_ds_11n_cfg *) req->pbuf;
    cfg_11n->sub_command = MLAN_OID_11N_CFG_AGGR_PRIO_TBL;
    req->req_id = MLAN_IOCTL_11N_CFG;

    if (wrq->u.data.length == 0) {
        /* Get aggr priority table from MLAN */
        req->action = MLAN_ACT_GET;
        if (MLAN_STATUS_SUCCESS !=
            woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            goto error;
        }
        wrq->u.data.length = MAX_NUM_TID * 2;
        for (i = 0, j = 0; i < (wrq->u.data.length); i = i + 2, ++j) {
            data[i] = cfg_11n->param.aggr_prio_tbl.ampdu[j];
            data[i + 1] = cfg_11n->param.aggr_prio_tbl.amsdu[j];
        }

        if (copy_to_user(wrq->u.data.pointer, data,
                         sizeof(int) * wrq->u.data.length)) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
            goto error;
        }
    } else if (wrq->u.data.length == 16) {
        if (copy_from_user(data, wrq->u.data.pointer,
                           sizeof(int) * wrq->u.data.length)) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto error;
        }
        for (i = 0, j = 0; i < (wrq->u.data.length); i = i + 2, ++j) {
            cfg_11n->param.aggr_prio_tbl.ampdu[j] = data[i];
            cfg_11n->param.aggr_prio_tbl.amsdu[j] = data[i + 1];
        }

        /* Update aggr priority table in MLAN */
        req->action = MLAN_ACT_SET;
        if (MLAN_STATUS_SUCCESS !=
            woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            goto error;
        }
    } else {
        PRINTM(ERROR, "Invalid number of arguments\n");
        ret = -EINVAL;
        goto error;
    }

  error:
    if (req)
        kfree(req);

    LEAVE();
    return ret;
}

/**
 *  @brief Set/Get add BA Reject paramters
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq	    A pointer to iwreq structure
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_addba_reject(moal_private * priv, struct iwreq *wrq)
{
    int data[MAX_NUM_TID], ret = 0, i;
    mlan_ioctl_req *req = NULL;
    mlan_ds_11n_cfg *cfg_11n = NULL;

    ENTER();

    PRINTM(ERROR, "%s\n", __FUNCTION__);
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
    if (req == NULL) {
        LEAVE();
        return -ENOMEM;
    }
    cfg_11n = (mlan_ds_11n_cfg *) req->pbuf;
    cfg_11n->sub_command = MLAN_OID_11N_CFG_ADDBA_REJECT;
    req->req_id = MLAN_IOCTL_11N_CFG;

    if (wrq->u.data.length == 0) {
        PRINTM(ERROR, "Addba reject moal\n");
        /* Get aggr priority table from MLAN */
        req->action = MLAN_ACT_GET;
        if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req,
                                                      MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            goto error;
        }

        wrq->u.data.length = MAX_NUM_TID;
        for (i = 0; i < (wrq->u.data.length); ++i) {
            data[i] = cfg_11n->param.addba_reject[i];
        }

        if (copy_to_user(wrq->u.data.pointer, data,
                         sizeof(int) * wrq->u.data.length)) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
            goto error;
        }
    } else if (wrq->u.data.length == 8) {
        if (copy_from_user(data, wrq->u.data.pointer,
                           sizeof(int) * wrq->u.data.length)) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto error;
        }
        for (i = 0; i < (wrq->u.data.length); ++i) {
            cfg_11n->param.addba_reject[i] = data[i];
        }

        /* Update aggr priority table in MLAN */
        req->action = MLAN_ACT_SET;
        if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req,
                                                      MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            goto error;
        }
    } else {
        PRINTM(ERROR, "Invalid number of arguments\n");
        ret = -EINVAL;
        goto error;
    }
  error:
    if (req)
        kfree(req);

    LEAVE();
    return ret;
}

/**
 *  @brief Set/Get add BA paramters
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq	    A pointer to iwreq structure
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_addba_para_updt(moal_private * priv, struct iwreq *wrq)
{
    int data[3], ret = 0;
    mlan_ioctl_req *req = NULL;
    mlan_ds_11n_cfg *cfg_11n = NULL;

    ENTER();

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
    if (req == NULL) {
        LEAVE();
        return -ENOMEM;
    }
    cfg_11n = (mlan_ds_11n_cfg *) req->pbuf;
    cfg_11n->sub_command = MLAN_OID_11N_CFG_ADDBA_PARAM;
    req->req_id = MLAN_IOCTL_11N_CFG;

    if (wrq->u.data.length == 0) {
        /* Get Add BA parameters from MLAN */
        req->action = MLAN_ACT_GET;
        if (MLAN_STATUS_SUCCESS !=
            woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            goto error;
        }
        data[0] = cfg_11n->param.addba_param.timeout;
        data[1] = cfg_11n->param.addba_param.txwinsize;
        data[2] = cfg_11n->param.addba_param.rxwinsize;
        PRINTM(INFO, "GET: timeout:%d txwinsize:%d rxwinsize:%d\n", data[0],
               data[1], data[2]);

        wrq->u.data.length = 3;
        if (copy_to_user(wrq->u.data.pointer, data,
                         wrq->u.data.length * sizeof(int))) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
            goto error;
        }
    } else if (wrq->u.data.length == 3) {
        if (copy_from_user
            (data, wrq->u.data.pointer, wrq->u.data.length * sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto error;
        }
        cfg_11n->param.addba_param.timeout = data[0];
        cfg_11n->param.addba_param.txwinsize = data[1];
        cfg_11n->param.addba_param.rxwinsize = data[2];
        PRINTM(INFO, "SET: timeout:%d txwinsize:%d rxwinsize:%d\n", data[0],
               data[1], data[2]);

        /* Update Add BA parameters in MLAN */
        req->action = MLAN_ACT_SET;
        if (MLAN_STATUS_SUCCESS !=
            woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
            ret = MLAN_STATUS_FAILURE;
            goto error;
        }
    } else {
        PRINTM(ERROR, "Invalid number of arguments\n");
        ret = -EINVAL;
        goto error;
    }

  error:
    if (req)
        kfree(req);

    LEAVE();
    return ret;
}

/**
 *  @brief Set/Get Transmit buffer size
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq	    A pointer to iwreq structure
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_txbuf_cfg(moal_private * priv, struct iwreq *wrq)
{
    int buf_size;
    mlan_ioctl_req *req = NULL;
    mlan_ds_11n_cfg *cfg_11n = NULL;
    int ret = 0;

    ENTER();
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    cfg_11n = (mlan_ds_11n_cfg *) req->pbuf;
    cfg_11n->sub_command = MLAN_OID_11N_CFG_MAX_TX_BUF_SIZE;
    req->req_id = MLAN_IOCTL_11N_CFG;

    if (wrq->u.data.length == 0) {
        /* Get Tx buffer size from MLAN */
        req->action = MLAN_ACT_GET;
    } else {
        if (copy_from_user(&buf_size, wrq->u.data.pointer, sizeof(buf_size))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        PRINTM(INFO, "SET: Tx buffer size %d\n", buf_size);
        /* Update Tx buffer size in MLAN */
        req->action = MLAN_ACT_SET;
        cfg_11n->param.tx_buf_size = buf_size;
    }
    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }
    buf_size = cfg_11n->param.tx_buf_size;
    if (copy_to_user(wrq->u.data.pointer, &buf_size, sizeof(buf_size))) {
        PRINTM(ERROR, "Copy to user failed\n");
        ret = -EFAULT;
        goto done;
    }
    wrq->u.data.length = 1;
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Set/Get Host Sleep configuration
 *
 *  @param priv             A pointer to moal_private structure
 *  @param wrq	            A pointer to iwreq structure
 *  @param invoke_hostcmd	MTRUE --invoke HostCmd, otherwise MFALSE
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_hs_cfg(moal_private * priv, struct iwreq *wrq, BOOLEAN invoke_hostcmd)
{
    int data[3];
    int ret = 0;
    mlan_ioctl_req *req = NULL;
    mlan_ds_pm_cfg *pmcfg = NULL;
    mlan_ds_hs_cfg hscfg;
    int data_length = wrq->u.data.length;

    ENTER();

    memset(data, 0, sizeof(data));
    memset(&hscfg, 0, sizeof(mlan_ds_hs_cfg));

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_pm_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    pmcfg = (mlan_ds_pm_cfg *) req->pbuf;
    pmcfg->sub_command = MLAN_OID_PM_CFG_HS_CFG;
    req->req_id = MLAN_IOCTL_PM_CFG;

    if (data_length == 0) {
        req->action = MLAN_ACT_GET;
    } else {
        req->action = MLAN_ACT_SET;
        if (data_length >= 1 && data_length <= 3) {
            if (copy_from_user
                (data, wrq->u.data.pointer, sizeof(int) * data_length)) {
                PRINTM(ERROR, "Copy from user failed\n");
                ret = -EFAULT;
                goto done;
            }
        } else {
            PRINTM(ERROR, "Invalid arguments\n");
            ret = -EINVAL;
            goto done;
        }
    }

    /* Do a GET first if all arguments are not available */
    if (data_length >= 1 && data_length < 3) {
        woal_get_hs_params(priv, MOAL_IOCTL_WAIT, &hscfg);
    }

    switch (data_length) {
    case 3:                    /* Conditions, GPIO, GAP provided */
        pmcfg->param.hs_cfg.conditions = data[0];
        pmcfg->param.hs_cfg.gpio = data[1];
        pmcfg->param.hs_cfg.gap = data[2];
        break;
    case 2:                    /* Conditions, GPIO provided */
        pmcfg->param.hs_cfg.conditions = data[0];
        pmcfg->param.hs_cfg.gpio = data[1];
        pmcfg->param.hs_cfg.gap = hscfg.gap;
        break;
    case 1:                    /* Conditions provided */
        pmcfg->param.hs_cfg.conditions = data[0];
        pmcfg->param.hs_cfg.gpio = hscfg.gpio;
        pmcfg->param.hs_cfg.gap = hscfg.gap;
        break;
    case 0:
        break;
    default:
        break;
    }

    pmcfg->param.hs_cfg.is_invoke_hostcmd = invoke_hostcmd;
    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    if (req->action == MLAN_ACT_GET) {
        data[0] = pmcfg->param.hs_cfg.conditions;
        data[1] = pmcfg->param.hs_cfg.gpio;
        data[2] = pmcfg->param.hs_cfg.gap;
        wrq->u.data.length = 3;
        if (copy_to_user
            (wrq->u.data.pointer, data, sizeof(int) * wrq->u.data.length)) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
            goto done;
        }
    }
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Set Host Sleep parameters
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq	        A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_hs_setpara(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    int data_length = wrq->u.data.length;

    ENTER();

    if (data_length >= 1 && data_length <= 3) {
        ret = woal_hs_cfg(priv, wrq, MFALSE);
        goto done;
    } else {
        PRINTM(ERROR, "Invalid arguments\n");
        ret = -EINVAL;
        goto done;
    }
  done:
    LEAVE();
    return ret;
}

/**
 *  @brief Get/Set inactivity timeout extend
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq	        A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_inactivity_timeout_ext(moal_private * priv, struct iwreq *wrq)
{
    int data[4];
    int ret = 0;
    mlan_ioctl_req *req = NULL;
    mlan_ds_pm_cfg *pmcfg = NULL;
    pmlan_ds_inactivity_to inac_to = NULL;
    int data_length = wrq->u.data.length;

    ENTER();

    memset(data, 0, sizeof(data));
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_pm_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    pmcfg = (mlan_ds_pm_cfg *) req->pbuf;
    inac_to = &pmcfg->param.inactivity_to;
    pmcfg->sub_command = MLAN_OID_PM_CFG_INACTIVITY_TO;
    req->req_id = MLAN_IOCTL_PM_CFG;

    if ((data_length != 0) && (data_length != 3) && (data_length != 4)) {
        ret = -EINVAL;
        goto done;
    }

    req->action = MLAN_ACT_GET;
    if (data_length) {
        if (copy_from_user
            (data, wrq->u.data.pointer, sizeof(int) * data_length)) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        req->action = MLAN_ACT_SET;
        inac_to->timeout_unit = data[0];
        inac_to->unicast_timeout = data[1];
        inac_to->mcast_timeout = data[2];
        inac_to->ps_entry_timeout = data[3];
    }

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    /* Copy back current values regardless of GET/SET */
    data[0] = inac_to->timeout_unit;
    data[1] = inac_to->unicast_timeout;
    data[2] = inac_to->mcast_timeout;
    data[3] = inac_to->ps_entry_timeout;

    if (req->action == MLAN_ACT_GET) {
        wrq->u.data.length = 4;
        if (copy_to_user
            (wrq->u.data.pointer, data, wrq->u.data.length * sizeof(int))) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
            goto done;
        }
    }

  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Set/Get system clock
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq	        A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_ecl_sys_clock(moal_private * priv, struct iwreq *wrq)
{
    int data[64];
    int ret = 0;
    mlan_ioctl_req *req = NULL;
    mlan_ds_misc_cfg *cfg = NULL;
    int data_length = wrq->u.data.length;
    int i = 0;

    ENTER();

    memset(data, 0, sizeof(data));

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    cfg = (mlan_ds_misc_cfg *) req->pbuf;
    cfg->sub_command = MLAN_OID_MISC_SYS_CLOCK;
    req->req_id = MLAN_IOCTL_MISC_CFG;

    if (!data_length)
        req->action = MLAN_ACT_GET;
    else if (data_length <= MLAN_MAX_CLK_NUM) {
        req->action = MLAN_ACT_SET;
        if (copy_from_user
            (data, wrq->u.data.pointer, sizeof(int) * data_length)) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
    } else {
        PRINTM(ERROR, "Invalid arguments\n");
        ret = -EINVAL;
        goto done;
    }

    if (req->action == MLAN_ACT_GET) {
        /* Get configurable clocks */
        cfg->param.sys_clock.sys_clk_type = MLAN_CLK_CONFIGURABLE;
        if (MLAN_STATUS_SUCCESS !=
            woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            goto done;
        }

        /* Current system clock */
        data[0] = (int) cfg->param.sys_clock.cur_sys_clk;
        wrq->u.data.length = 1;

        data_length = MIN(cfg->param.sys_clock.sys_clk_num, MLAN_MAX_CLK_NUM);

        /* Configurable clocks */
        for (i = 0; i < data_length; i++) {
            data[i + wrq->u.data.length] =
                (int) cfg->param.sys_clock.sys_clk[i];
        }
        wrq->u.data.length += data_length;

        /* Get supported clocks */
        cfg->param.sys_clock.sys_clk_type = MLAN_CLK_SUPPORTED;
        if (MLAN_STATUS_SUCCESS !=
            woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            goto done;
        }

        data_length = MIN(cfg->param.sys_clock.sys_clk_num, MLAN_MAX_CLK_NUM);

        /* Supported clocks */
        for (i = 0; i < data_length; i++) {
            data[i + wrq->u.data.length] =
                (int) cfg->param.sys_clock.sys_clk[i];
        }

        wrq->u.data.length += data_length;

        if (copy_to_user
            (wrq->u.data.pointer, data, sizeof(int) * wrq->u.data.length)) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
            goto done;
        }
    } else {
        /* Set configurable clocks */
        cfg->param.sys_clock.sys_clk_type = MLAN_CLK_CONFIGURABLE;
        cfg->param.sys_clock.sys_clk_num = MIN(MLAN_MAX_CLK_NUM, data_length);
        for (i = 0; i < cfg->param.sys_clock.sys_clk_num; i++) {
            cfg->param.sys_clock.sys_clk[i] = (t_u16) data[i];
        }

        if (MLAN_STATUS_SUCCESS !=
            woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            goto done;
        }
    }
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Set/Get Band and Adhoc-band setting
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq	        A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_band_cfg(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0, i;
    int data[3];
    int user_data_len = wrq->u.data.length;
    t_u32 infra_band = 0;
    t_u32 adhoc_band = 0;
    t_u32 adhoc_channel = 0;
    mlan_ioctl_req *req = NULL;
    mlan_ds_radio_cfg *radio_cfg = NULL;

    ENTER();

    if (user_data_len > 3) {
        LEAVE();
        return -EINVAL;
    }

    if (user_data_len > 0) {
        if (priv->media_connected == MTRUE) {
            LEAVE();
            return -EOPNOTSUPP;
        }
    }

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_radio_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto error;
    }
    radio_cfg = (mlan_ds_radio_cfg *) req->pbuf;
    radio_cfg->sub_command = MLAN_OID_BAND_CFG;
    req->req_id = MLAN_IOCTL_RADIO_CFG;

    if (wrq->u.data.length == 0) {
        /* Get config_bands, adhoc_start_band and adhoc_channnel values from
           MLAN */
        req->action = MLAN_ACT_GET;
        if (MLAN_STATUS_SUCCESS !=
            woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            goto error;
        }
        data[0] = radio_cfg->param.band_cfg.config_bands;       /* Infra Band */
        data[1] = radio_cfg->param.band_cfg.adhoc_start_band;   /* Adhoc Band */
        data[2] = radio_cfg->param.band_cfg.adhoc_channel;      /* Adhoc
                                                                   Channel */

        wrq->u.data.length = 3;
        if (copy_to_user
            (wrq->u.data.pointer, data, sizeof(int) * wrq->u.data.length)) {
            ret = -EFAULT;
            goto error;
        }
    } else {
        if (copy_from_user
            (data, wrq->u.data.pointer, sizeof(int) * wrq->u.data.length)) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto error;
        }

        /* To support only <b/bg/bgn/n> */
        infra_band = data[0];
        for (i = 0; i < sizeof(SupportedInfraBand); i++)
            if (infra_band == SupportedInfraBand[i])
                break;
        if (i == sizeof(SupportedInfraBand)) {
            ret = -EINVAL;
            goto error;
        }

        /* Set Adhoc band */
        if (user_data_len >= 2) {
            adhoc_band = data[1];
            for (i = 0; i < sizeof(SupportedAdhocBand); i++)
                if (adhoc_band == SupportedAdhocBand[i])
                    break;
            if (i == sizeof(SupportedAdhocBand)) {
                ret = -EINVAL;
                goto error;
            }
        }

        /* Set Adhoc channel */
        if (user_data_len == 3) {
            adhoc_channel = data[2];
        }

        /* Set config_bands and adhoc_start_band values to MLAN */
        req->action = MLAN_ACT_SET;
        radio_cfg->param.band_cfg.config_bands = infra_band;
        radio_cfg->param.band_cfg.adhoc_start_band = adhoc_band;
        radio_cfg->param.band_cfg.adhoc_channel = adhoc_channel;
        if (MLAN_STATUS_SUCCESS !=
            woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
            ret = -EFAULT;
            goto error;
        }
    }

  error:
    if (req)
        kfree(req);

    LEAVE();
    return ret;
}

/**
 *  @brief Read/Write adapter registers value 
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq	        A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_reg_read_write(moal_private * priv, struct iwreq *wrq)
{
    int data[3];
    int ret = 0;
    mlan_ioctl_req *req = NULL;
    mlan_ds_reg_mem *reg = NULL;
    int data_length = wrq->u.data.length;

    ENTER();

    memset(data, 0, sizeof(data));

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_reg_mem));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    reg = (mlan_ds_reg_mem *) req->pbuf;
    reg->sub_command = MLAN_OID_REG_RW;
    req->req_id = MLAN_IOCTL_REG_MEM;

    if (data_length == 2) {
        req->action = MLAN_ACT_GET;
    } else if (data_length == 3) {
        req->action = MLAN_ACT_SET;
    } else {
        ret = -EINVAL;
        goto done;
    }
    if (copy_from_user(data, wrq->u.data.pointer, sizeof(int) * data_length)) {
        PRINTM(ERROR, "Copy from user failed\n");
        ret = -EFAULT;
        goto done;
    }
    reg->param.reg_rw.type = (t_u32) data[0];
    if ((reg->param.reg_rw.type < MLAN_REG_MAC) ||
        (reg->param.reg_rw.type > MLAN_REG_PMIC)
        ) {
        ret = -EINVAL;
        goto done;
    }

    reg->param.reg_rw.offset = (t_u32) data[1];
    if (data_length == 3)
        reg->param.reg_rw.value = (t_u32) data[2];

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    if (req->action == MLAN_ACT_GET) {
        if (copy_to_user
            (wrq->u.data.pointer, &reg->param.reg_rw.value, sizeof(int))) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
            goto done;
        }
        wrq->u.data.length = 1;
    }

  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Read the EEPROM contents of the card 
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq	        A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_read_eeprom(moal_private * priv, struct iwreq *wrq)
{
    int data[2];
    int ret = 0;
    mlan_ioctl_req *req = NULL;
    mlan_ds_reg_mem *reg = NULL;
    int data_length = wrq->u.data.length;

    ENTER();

    memset(data, 0, sizeof(data));

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_reg_mem));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    reg = (mlan_ds_reg_mem *) req->pbuf;
    reg->sub_command = MLAN_OID_EEPROM_RD;
    req->req_id = MLAN_IOCTL_REG_MEM;

    if (data_length == 2) {
        req->action = MLAN_ACT_GET;
    } else {
        ret = -EINVAL;
        goto done;
    }
    if (copy_from_user(data, wrq->u.data.pointer, sizeof(int) * data_length)) {
        PRINTM(ERROR, "Copy from user failed\n");
        ret = -EFAULT;
        goto done;
    }

    reg->param.rd_eeprom.offset = (t_u16) data[0];
    reg->param.rd_eeprom.byte_count = (t_u16) data[1];

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    if (req->action == MLAN_ACT_GET) {
        wrq->u.data.length = reg->param.rd_eeprom.byte_count;
        if (copy_to_user
            (wrq->u.data.pointer, reg->param.rd_eeprom.value,
             wrq->u.data.length)) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
            goto done;
        }
    }

  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Get LOG
 *
 *  @param priv                 A pointer to moal_private structure
 *  @param wrq			A pointer to iwreq structure
 *
 *  @return 	   	 	0 --success, otherwise fail
 */
static int
woal_get_log(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_get_stats stats;
    char *buf = NULL;

    ENTER();

    PRINTM(INFO, " GET STATS\n");
    if (!(buf = kmalloc(GETLOG_BUFSIZE, GFP_ATOMIC))) {
        PRINTM(ERROR, "kmalloc failed!\n");
        ret = -ENOMEM;
        goto done;
    }

    memset(&stats, 0, sizeof(mlan_ds_get_stats));
    if (MLAN_STATUS_SUCCESS !=
        woal_get_stats_info(priv, MOAL_IOCTL_WAIT, &stats)) {
        ret = -EFAULT;
        goto done;
    }

    if (wrq->u.data.pointer) {
        sprintf(buf, "\n"
                "mcasttxframe     %lu\n"
                "failed           %lu\n"
                "retry            %lu\n"
                "multiretry       %lu\n"
                "framedup         %lu\n"
                "rtssuccess       %lu\n"
                "rtsfailure       %lu\n"
                "ackfailure       %lu\n"
                "rxfrag           %lu\n"
                "mcastrxframe     %lu\n"
                "fcserror         %lu\n"
                "txframe          %lu\n"
                "wepicverrcnt-1   %lu\n"
                "wepicverrcnt-2   %lu\n"
                "wepicverrcnt-3   %lu\n"
                "wepicverrcnt-4   %lu\n",
                stats.mcast_tx_frame,
                stats.failed,
                stats.retry,
                stats.multi_retry,
                stats.frame_dup,
                stats.rts_success,
                stats.rts_failure,
                stats.ack_failure,
                stats.rx_frag,
                stats.mcast_rx_frame,
                stats.fcs_error,
                stats.tx_frame,
                stats.wep_icv_error[0],
                stats.wep_icv_error[1],
                stats.wep_icv_error[2], stats.wep_icv_error[3]);
        wrq->u.data.length = strlen(buf) + 1;
        if (copy_to_user(wrq->u.data.pointer, buf, wrq->u.data.length)) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
        }
    }
  done:
    if (buf)
        kfree(buf);
    LEAVE();
    return ret;
}

/**
 *  @brief Deauthenticate
 *
 *  @param priv                 A pointer to moal_private structure
 *  @param wrq			A pointer to iwreq structure
 *
 *  @return 	   	 	0 --success, otherwise fail
 */
static int
woal_deauth(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    struct sockaddr saddr;

    ENTER();
    if (wrq->u.data.length) {
        /* Deauth mentioned BSSID */
        if (copy_from_user(&saddr, wrq->u.data.pointer, sizeof(saddr))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        if (MLAN_STATUS_SUCCESS !=
            woal_disconnect(priv, (t_u8 *) saddr.sa_data))
            ret = -EFAULT;
    } else {
        if (MLAN_STATUS_SUCCESS != woal_disconnect(priv, NULL))
            ret = -EFAULT;
    }
  done:
    LEAVE();
    return ret;
}

/**
 *  @brief Set/Get TX power configurations
 *  
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *  
 *  @return         0 --success, otherwise fail
 */
static int
woal_tx_power_cfg(moal_private * priv, struct iwreq *wrq)
{
    int data[5], user_data_len;
    int ret = 0;
    mlan_bss_info bss_info;
    mlan_ds_power_cfg *pcfg = NULL;
    mlan_ioctl_req *req = NULL;
    ENTER();

    memset(&bss_info, 0, sizeof(bss_info));
    woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);

    memset(data, 0, sizeof(data));
    user_data_len = wrq->u.data.length;

    if (user_data_len) {
        if (copy_from_user
            (data, wrq->u.data.pointer, sizeof(int) * user_data_len)) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        switch (user_data_len) {
        case 1:
            if (data[0] != 0xFF)
                ret = -EINVAL;
            break;
        case 2:
        case 4:
            if (data[0] == 0xFF) {
                ret = -EINVAL;
                break;
            }
            if (data[1] < bss_info.min_power_level) {
                PRINTM(ERROR,
                       "The set powercfg rate value %d dBm is out of range (%d dBm-%d dBm)!\n",
                       data[1], (int) bss_info.min_power_level,
                       (int) bss_info.max_power_level);
                ret = -EINVAL;
                break;
            }
            if (user_data_len == 4) {
                if (data[1] > data[2]) {
                    PRINTM(ERROR, "Min power should be less than maximum!\n");
                    ret = -EINVAL;
                    break;
                }
                if (data[3] < 0) {
                    PRINTM(ERROR, "Step should not less than 0!\n");
                    ret = -EINVAL;
                    break;
                }
                if (data[2] > bss_info.max_power_level) {
                    PRINTM(ERROR,
                           "The set powercfg rate value %d dBm is out of range (%d dBm-%d dBm)!\n",
                           data[2], (int) bss_info.min_power_level,
                           (int) bss_info.max_power_level);
                    ret = -EINVAL;
                    break;
                }
                if (data[3] > data[2] - data[1]) {
                    PRINTM(ERROR,
                           "Step should not greater than power difference!\n");
                    ret = -EINVAL;
                    break;
                }
            }
            break;
        default:
            ret = -EINVAL;
            break;
        }
        if (ret)
            goto done;
    }

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_power_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    pcfg = (mlan_ds_power_cfg *) req->pbuf;
    pcfg->sub_command = MLAN_OID_POWER_CFG_EXT;
    req->req_id = MLAN_IOCTL_POWER_CFG;
    if (!user_data_len)
        req->action = MLAN_ACT_GET;
    else {
        req->action = MLAN_ACT_SET;
        pcfg->param.power_ext.len = user_data_len;
        memcpy((t_u8 *) & pcfg->param.power_ext.power_data, (t_u8 *) data,
               sizeof(data));
    }
    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }
    if (!user_data_len) {
        if (copy_to_user
            (wrq->u.data.pointer, (t_u8 *) & pcfg->param.power_ext.power_data,
             sizeof(int) * pcfg->param.power_ext.len)) {
            ret = -EFAULT;
            goto done;
        }
        wrq->u.data.length = pcfg->param.power_ext.len;
    }
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Get Tx/Rx data rates
 *  
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *  
 *  @return         0 --success, otherwise fail
 */
static int
woal_get_txrx_rate(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_rate *rate = NULL;
    mlan_ioctl_req *req = NULL;

    ENTER();

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_rate));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    rate = (mlan_ds_rate *) req->pbuf;
    rate->sub_command = MLAN_OID_GET_DATA_RATE;
    req->req_id = MLAN_IOCTL_RATE;
    req->action = MLAN_ACT_GET;

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    if (copy_to_user
        (wrq->u.data.pointer, (t_u8 *) & rate->param.data_rate,
         sizeof(int) * 2)) {
        ret = -EFAULT;
        goto done;
    }
    wrq->u.data.length = 2;
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Set/Get beacon interval
 *  
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *  
 *  @return         0 --success, otherwise fail
 */
static int
woal_beacon_interval(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_bss *bss = NULL;
    mlan_ioctl_req *req = NULL;
    int bcn = 0;

    ENTER();

    if (wrq->u.data.length) {
        if (copy_from_user(&bcn, wrq->u.data.pointer, sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        if ((bcn < MLAN_MIN_BEACON_INTERVAL) ||
            (bcn > MLAN_MAX_BEACON_INTERVAL)) {
            ret = -EINVAL;
            goto done;
        }
    }

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_bss));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    bss = (mlan_ds_bss *) req->pbuf;
    bss->sub_command = MLAN_OID_IBSS_BCN_INTERVAL;
    req->req_id = MLAN_IOCTL_BSS;
    if (!wrq->u.data.length)
        req->action = MLAN_ACT_GET;
    else {
        req->action = MLAN_ACT_SET;
        bss->param.bcn_interval = bcn;
    }

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    if (copy_to_user
        (wrq->u.data.pointer, (t_u8 *) & bss->param.bcn_interval,
         sizeof(int))) {
        ret = -EFAULT;
        goto done;
    }
    wrq->u.data.length = 1;
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Set/Get ATIM window
 *  
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *  
 *  @return         0 --success, otherwise fail
 */
static int
woal_atim_window(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_bss *bss = NULL;
    mlan_ioctl_req *req = NULL;
    int atim = 0;

    ENTER();

    if (wrq->u.data.length) {
        if (copy_from_user(&atim, wrq->u.data.pointer, sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        if ((atim < 0) || (atim > MLAN_MAX_ATIM_WINDOW)) {
            ret = -EINVAL;
            goto done;
        }
    }

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_bss));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    bss = (mlan_ds_bss *) req->pbuf;
    bss->sub_command = MLAN_OID_IBSS_ATIM_WINDOW;
    req->req_id = MLAN_IOCTL_BSS;
    if (!wrq->u.data.length)
        req->action = MLAN_ACT_GET;
    else {
        req->action = MLAN_ACT_SET;
        bss->param.atim_window = atim;
    }

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    if (copy_to_user
        (wrq->u.data.pointer, (t_u8 *) & bss->param.atim_window, sizeof(int))) {
        ret = -EFAULT;
        goto done;
    }
    wrq->u.data.length = 1;
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 * @brief Set/Get TX data rate
 * 
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 * 
 * @return           0 --success, otherwise fail
 */
static int
woal_set_get_txrate(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_rate *rate = NULL;
    mlan_ioctl_req *req = NULL;
    int rateindex = 0;

    ENTER();
    if (wrq->u.data.length) {
        if (copy_from_user(&rateindex, wrq->u.data.pointer, sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
    }
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_rate));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    rate = (mlan_ds_rate *) req->pbuf;
    rate->param.rate_cfg.rate_type = MLAN_RATE_INDEX;
    rate->sub_command = MLAN_OID_RATE_CFG;
    req->req_id = MLAN_IOCTL_RATE;
    if (!wrq->u.data.length)
        req->action = MLAN_ACT_GET;
    else {
        req->action = MLAN_ACT_SET;
        if (rateindex == AUTO_RATE)
            rate->param.rate_cfg.is_rate_auto = 1;
        else {
            if ((rateindex != MLAN_RATE_INDEX_MCS32) &&
                ((rateindex < 0) || (rateindex > MLAN_RATE_INDEX_MCS7))) {
                ret = -EINVAL;
                goto done;
            }
        }
        rate->param.rate_cfg.rate = rateindex;
    }

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    } else {
        if (wrq->u.data.length)
            priv->rate_index = rateindex;
    }
    if (!wrq->u.data.length) {
        if (rate->param.rate_cfg.is_rate_auto)
            rateindex = AUTO_RATE;
        else
            rateindex = rate->param.rate_cfg.rate;
        wrq->u.data.length = 1;
        if (copy_to_user(wrq->u.data.pointer, &rateindex, sizeof(int))) {
            ret = -EFAULT;
        }
    }
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 * @brief Set/Get region code
 * 
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 * 
 * @return           0 --success, otherwise fail
 */
static int
woal_set_get_regioncode(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_misc_cfg *cfg = NULL;
    mlan_ioctl_req *req = NULL;
    int region = 0;

    ENTER();

    if (wrq->u.data.length) {
        if (copy_from_user(&region, wrq->u.data.pointer, sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
    }

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    cfg = (mlan_ds_misc_cfg *) req->pbuf;
    cfg->sub_command = MLAN_OID_MISC_REGION;
    req->req_id = MLAN_IOCTL_MISC_CFG;
    if (!wrq->u.data.length)
        req->action = MLAN_ACT_GET;
    else {
        req->action = MLAN_ACT_SET;
        cfg->param.region_code = region;
    }

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    if (!wrq->u.data.length) {
        wrq->u.data.length = 1;
        if (copy_to_user
            (wrq->u.data.pointer, &cfg->param.region_code, sizeof(int)))
            ret = -EFAULT;
    }
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 * @brief Set/Get radio
 * 
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 * 
 * @return           0 --success, otherwise fail
 */
static int
woal_set_get_radio(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_bss_info bss_info;
    int option = 0;

    ENTER();

    memset(&bss_info, 0, sizeof(bss_info));

    if (wrq->u.data.length) {
        /* Set radio */
        if (copy_from_user(&option, wrq->u.data.pointer, sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        if (MLAN_STATUS_SUCCESS != woal_set_radio(priv, (t_u8) option))
            ret = -EFAULT;
    } else {
        /* Get radio status */
        woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);
        wrq->u.data.length = 1;
        if (copy_to_user
            (wrq->u.data.pointer, &bss_info.radio_on,
             sizeof(bss_info.radio_on))) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
        }
    }
  done:
    LEAVE();
    return ret;
}

#ifdef DEBUG_LEVEL1
/** 
 *  @brief Get/Set the bit mask of driver debug message control
 *
 *  @param priv			A pointer to moal_private structure
 *  @param wrq			A pointer to wrq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_drv_dbg(moal_private * priv, struct iwreq *wrq)
{
    int data[4];
    int ret = 0;

    ENTER();

    if (!wrq->u.data.length) {
        data[0] = drvdbg;
        data[1] = ifdbg;
        /* Return the current driver debug bit masks */
        if (copy_to_user(wrq->u.data.pointer, data, sizeof(int) * 2)) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
            goto drvdbgexit;
        }
        wrq->u.data.length = 2;
    } else if (wrq->u.data.length < 3) {
        /* Get the driver debug bit masks */
        if (copy_from_user
            (data, wrq->u.data.pointer, sizeof(int) * wrq->u.data.length)) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto drvdbgexit;
        }
        drvdbg = data[0];
        if (wrq->u.data.length == 2)
            ifdbg = data[1];
    } else {
        PRINTM(ERROR, "Invalid parameter number\n");
        goto drvdbgexit;
    }

    printk(KERN_ALERT "drvdbg = 0x%08lx\n", drvdbg);
#ifdef DEBUG_LEVEL2
    printk(KERN_ALERT "INFO  (%08lx) %s\n", DBG_INFO,
           (drvdbg & DBG_INFO) ? "X" : "");
    printk(KERN_ALERT "WARN  (%08lx) %s\n", DBG_WARN,
           (drvdbg & DBG_WARN) ? "X" : "");
    printk(KERN_ALERT "ENTRY (%08lx) %s\n", DBG_ENTRY,
           (drvdbg & DBG_ENTRY) ? "X" : "");
#endif
    printk(KERN_ALERT "FW_D  (%08lx) %s\n", DBG_FW_D,
           (drvdbg & DBG_FW_D) ? "X" : "");
    printk(KERN_ALERT "CMD_D (%08lx) %s\n", DBG_CMD_D,
           (drvdbg & DBG_CMD_D) ? "X" : "");
    printk(KERN_ALERT "DAT_D (%08lx) %s\n", DBG_DAT_D,
           (drvdbg & DBG_DAT_D) ? "X" : "");
    printk(KERN_ALERT "INTR  (%08lx) %s\n", DBG_INTR,
           (drvdbg & DBG_INTR) ? "X" : "");
    printk(KERN_ALERT "EVENT (%08lx) %s\n", DBG_EVENT,
           (drvdbg & DBG_EVENT) ? "X" : "");
    printk(KERN_ALERT "CMND  (%08lx) %s\n", DBG_CMND,
           (drvdbg & DBG_CMND) ? "X" : "");
    printk(KERN_ALERT "DATA  (%08lx) %s\n", DBG_DATA,
           (drvdbg & DBG_DATA) ? "X" : "");
    printk(KERN_ALERT "ERROR (%08lx) %s\n", DBG_ERROR,
           (drvdbg & DBG_ERROR) ? "X" : "");
    printk(KERN_ALERT "FATAL (%08lx) %s\n", DBG_FATAL,
           (drvdbg & DBG_FATAL) ? "X" : "");
    printk(KERN_ALERT "MSG   (%08lx) %s\n", DBG_MSG,
           (drvdbg & DBG_MSG) ? "X" : "");
    printk(KERN_ALERT "ifdbg = 0x%08lx\n", ifdbg);
    printk(KERN_ALERT "IF_D  (%08lx) %s\n", DBG_IF_D,
           (ifdbg & DBG_IF_D) ? "X" : "");

  drvdbgexit:
    LEAVE();
    return ret;
}
#endif

/**
 * @brief Set/Get Tx/Rx antenna
 * 
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 * 
 * @return         0 --success, otherwise fail
 */
static int
woal_set_get_tx_rx_ant(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_radio_cfg *radio = NULL;
    mlan_ioctl_req *req = NULL;
    int data = 0;

    ENTER();
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_radio_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    radio = (mlan_ds_radio_cfg *) req->pbuf;
    radio->sub_command = MLAN_OID_ANT_CFG;
    req->req_id = MLAN_IOCTL_RADIO_CFG;
    if (wrq->u.data.length) {
        if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        if (data != RF_ANTENNA_1 && data != RF_ANTENNA_2 &&
            data != RF_ANTENNA_AUTO) {
            ret = -EINVAL;
            goto done;
        }
        req->action = MLAN_ACT_SET;
        radio->param.antenna = data;
    } else
        req->action = MLAN_ACT_GET;
    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }
    if (!wrq->u.data.length) {
        data = radio->param.antenna;
        if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
            ret = -EFAULT;
            goto done;
        }
        wrq->u.data.length = 1;
    }
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 * @brief Set/Get QoS configuration
 * 
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 * 
 * @return         0 --success, otherwise fail
 */
static int
woal_set_get_qos_cfg(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_wmm_cfg *cfg = NULL;
    mlan_ioctl_req *req = NULL;
    int data = 0;

    ENTER();
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wmm_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    cfg = (mlan_ds_wmm_cfg *) req->pbuf;
    cfg->sub_command = MLAN_OID_WMM_CFG_QOS;
    req->req_id = MLAN_IOCTL_WMM_CFG;
    if (wrq->u.data.length) {
        if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        req->action = MLAN_ACT_SET;
        cfg->param.qos_cfg = (t_u8) data;
    } else
        req->action = MLAN_ACT_GET;
    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }
    if (!wrq->u.data.length) {
        data = (int) cfg->param.qos_cfg;
        if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
            ret = -EFAULT;
            goto done;
        }
        wrq->u.data.length = 1;
    }
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 * @brief Set/Get WWS mode
 * 
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 * 
 * @return         0 --success, otherwise fail
 */
static int
woal_wws_cfg(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_misc_cfg *wws = NULL;
    mlan_ioctl_req *req = NULL;
    int data = 0;

    ENTER();
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    wws = (mlan_ds_misc_cfg *) req->pbuf;
    wws->sub_command = MLAN_OID_MISC_WWS;
    req->req_id = MLAN_IOCTL_MISC_CFG;
    if (wrq->u.data.length) {
        if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        if (data != CMD_DISABLED && data != CMD_ENABLED) {
            ret = -EINVAL;
            goto done;
        }
        req->action = MLAN_ACT_SET;
        wws->param.wws_cfg = data;
    } else
        req->action = MLAN_ACT_GET;
    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }
    if (!wrq->u.data.length) {
        data = wws->param.wws_cfg;
        if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
            ret = -EFAULT;
            goto done;
        }
        wrq->u.data.length = 1;
    }
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

#ifdef REASSOCIATION
/**
 * @brief Set/Get reassociation settings
 * 
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 * 
 * @return         0 --success, otherwise fail
 */
static int
woal_set_get_reassoc(moal_private * priv, struct iwreq *wrq)
{
    moal_handle *handle = priv->phandle;
    int ret = 0;
    int data = 0;

    ENTER();

    if (wrq->u.data.length) {
        if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        if (data == 0) {
            handle->reassoc_on = MFALSE;
            if (handle->is_reassoc_timer_set == MTRUE) {
                woal_cancel_timer(&handle->reassoc_timer);
                handle->is_reassoc_timer_set = MFALSE;
            }
        } else
            handle->reassoc_on = MTRUE;
    } else {
        data = (int) handle->reassoc_on;
        if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
            ret = -EFAULT;
            goto done;
        }
        wrq->u.data.length = 1;
    }

  done:
    LEAVE();
    return ret;
}
#endif

/**
 *  @brief implement WMM enable command
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      Pointer to user data
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_wmm_enable_ioctl(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_wmm_cfg *wmm = NULL;
    mlan_ioctl_req *req = NULL;
    int data = 0;

    ENTER();

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wmm_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    wmm = (mlan_ds_wmm_cfg *) req->pbuf;
    req->req_id = MLAN_IOCTL_WMM_CFG;
    wmm->sub_command = MLAN_OID_WMM_CFG_ENABLE;

    if (wrq->u.data.length) {
        /* Set WMM configuration */
        if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        if ((data < CMD_DISABLED) || (data > CMD_ENABLED)) {
            ret = -EINVAL;
            goto done;
        }
        req->action = MLAN_ACT_SET;
        if (data == CMD_DISABLED)
            wmm->param.wmm_enable = MFALSE;
        else
            wmm->param.wmm_enable = MTRUE;
    } else {
        /* Get WMM status */
        req->action = MLAN_ACT_GET;
    }

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    if (wrq->u.data.pointer) {
        if (copy_to_user
            (wrq->u.data.pointer, &wmm->param.wmm_enable,
             sizeof(wmm->param.wmm_enable))) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
            goto done;
        }
        wrq->u.data.length = 1;
    }
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Implement 802.11D enable command
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      Pointer to user data
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_11d_enable_ioctl(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_11d_cfg *pcfg_11d = NULL;
    mlan_ioctl_req *req = NULL;
    int data = 0;

    ENTER();

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11d_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    pcfg_11d = (mlan_ds_11d_cfg *) req->pbuf;
    req->req_id = MLAN_IOCTL_11D_CFG;
    pcfg_11d->sub_command = MLAN_OID_11D_CFG_ENABLE;
    if (wrq->u.data.length) {
        /* Set 11D configuration */
        if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        if ((data < CMD_DISABLED) || (data > CMD_ENABLED)) {
            ret = -EINVAL;
            goto done;
        }
        if (data == CMD_DISABLED)
            pcfg_11d->param.enable_11d = MFALSE;
        else
            pcfg_11d->param.enable_11d = MTRUE;
        req->action = MLAN_ACT_SET;
    } else {
        req->action = MLAN_ACT_GET;
    }

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    if (wrq->u.data.pointer) {
        if (copy_to_user
            (wrq->u.data.pointer, &pcfg_11d->param.enable_11d,
             sizeof(pcfg_11d->param.enable_11d))) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
            goto done;
        }
        wrq->u.data.length = 1;
    }
  done:
    if (req)
        kfree(req);

    LEAVE();
    return ret;
}

/**
 *  @brief Control WPS Session Enable/Disable
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      Pointer to user data
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_wps_cfg_ioctl(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_wps_cfg *pwps = NULL;
    mlan_ioctl_req *req = NULL;
    char buf[8];
    struct iwreq *wreq = (struct iwreq *) wrq;

    ENTER();

    PRINTM(INFO, "WOAL_WPS_SESSION\n");

    memset(buf, 0, sizeof(buf));
    if (copy_from_user(buf, wreq->u.data.pointer,
                       MIN(sizeof(buf) - 1, wreq->u.data.length))) {
        PRINTM(ERROR, "Copy from user failed\n");
        ret = -EFAULT;
        goto done;
    }

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wps_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    pwps = (mlan_ds_wps_cfg *) req->pbuf;
    req->req_id = MLAN_IOCTL_WPS_CFG;
    req->action = MLAN_ACT_SET;
    pwps->sub_command = MLAN_OID_WPS_CFG_SESSION;
    if (buf[0] == 1)
        pwps->param.wps_session = MLAN_WPS_CFG_SESSION_START;
    else
        pwps->param.wps_session = MLAN_WPS_CFG_SESSION_END;

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/** 
 *  @brief Convert ascii string to Hex integer
 *     
 *  @param d                    A pointer to integer buf
 *  @param s			A pointer to ascii string 
 *  @param dlen			The length o fascii string
 *
 *  @return 	   	        Number of integer  
 */
static int
woal_ascii2hex(t_u8 * d, char *s, t_u32 dlen)
{
    int i;
    t_u8 n;

    ENTER();

    memset(d, 0x00, dlen);

    for (i = 0; i < dlen * 2; i++) {
        if ((s[i] >= 48) && (s[i] <= 57))
            n = s[i] - 48;
        else if ((s[i] >= 65) && (s[i] <= 70))
            n = s[i] - 55;
        else if ((s[i] >= 97) && (s[i] <= 102))
            n = s[i] - 87;
        else
            break;
        if (!(i % 2))
            n = n * 16;
        d[i / 2] += n;
    }

    LEAVE();
    return i;
}

/** 
 *  @brief Extension of strsep lib command. This function will also take care
 *	   escape character
 *
 *  @param s         A pointer to array of chars to process
 *  @param delim     The delimiter character to end the string
 *  @param esc       The escape character to ignore for delimiter
 *
 *  @return          Pointer to the seperated string if delim found, else NULL
 */
static char *
woal_strsep(char **s, char delim, char esc)
{
    char *se = *s, *sb;

    ENTER();

    if (!(*s) || (*se == '\0')) {
        LEAVE();
        return NULL;
    }

    for (sb = *s; *sb != '\0'; ++sb) {
        if (*sb == esc && *(sb + 1) == esc) {
            /* 
             * We get a esc + esc seq then keep the one esc
             * and chop off the other esc character
             */
            memmove(sb, sb + 1, strlen(sb));
            continue;
        }
        if (*sb == esc && *(sb + 1) == delim) {
            /* 
             * We get a delim + esc seq then keep the delim
             * and chop off the esc character
             */
            memmove(sb, sb + 1, strlen(sb));
            continue;
        }
        if (*sb == delim)
            break;
    }

    if (*sb == '\0')
        sb = NULL;
    else
        *sb++ = '\0';

    *s = sb;

    LEAVE();
    return se;
}

/**
 *  @brief Convert mac address from string to t_u8 buffer.
 *
 *  @param mac_addr The buffer to store the mac address in.	    
 *  @param buf      The source of mac address which is a string.	    
 *
 *  @return 	    N/A
 */
static void
woal_mac2u8(t_u8 * mac_addr, char *buf)
{
    char *begin = buf, *end;
    int i;

    ENTER();

    for (i = 0; i < ETH_ALEN; ++i) {
        end = woal_strsep(&begin, ':', '/');
        if (end)
            mac_addr[i] = woal_atox(end);
    }

    LEAVE();
}

/**
 *  @brief Set WPA passphrase and SSID
 *
 *  @param priv	    A pointer to moal_private structure
 *  @param wrq	    A pointer to user data
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_passphrase(moal_private * priv, struct iwreq *wrq)
{
    t_u16 len = 0;
    static char buf[256];
    char *begin, *end, *opt;
    int ret = 0, action = -1, i;
    mlan_ds_sec_cfg *sec = NULL;
    mlan_ioctl_req *req = NULL;
    t_u8 zero_mac[] = { 0, 0, 0, 0, 0, 0 };
    t_u8 *mac = NULL;

    ENTER();

    if (!wrq->u.data.length) {
        PRINTM(ERROR, "Argument missing for setpassphrase\n");
        ret = -EINVAL;
        goto done;
    }

    if (copy_from_user(buf, wrq->u.data.pointer, wrq->u.data.length)) {
        PRINTM(ERROR, "Copy from user failed\n");
        ret = -EFAULT;
        goto done;
    }
    buf[wrq->u.data.length] = '\0';

    if (wrq->u.data.length <= 1) {
        PRINTM(ERROR, "No valid arguments\n");
        ret = -EINVAL;
        goto done;
    }
    /* Parse the buf to get the cmd_action */
    begin = buf;
    end = woal_strsep(&begin, ';', '/');
    if (end)
        action = woal_atox(end);
    if (action < 0 || action > 2 || end[1] != '\0') {
        PRINTM(ERROR, "Invalid action argument %s\n", end);
        ret = -EINVAL;
        goto done;
    }
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_sec_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    sec = (mlan_ds_sec_cfg *) req->pbuf;
    sec->sub_command = MLAN_OID_SEC_CFG_PASSPHRASE;
    req->req_id = MLAN_IOCTL_SEC_CFG;
    if (action == 0)
        req->action = MLAN_ACT_GET;
    else
        req->action = MLAN_ACT_SET;
    while (begin) {
        end = woal_strsep(&begin, ';', '/');
        opt = woal_strsep(&end, '=', '/');
        if (!opt || !end || !end[0]) {
            PRINTM(ERROR, "Invalid option\n");
            ret = -EINVAL;
            break;
        } else if (!strnicmp(opt, "ssid", strlen(opt))) {
            if (strlen(end) > MLAN_MAX_SSID_LENGTH) {
                PRINTM(ERROR, "SSID length exceeds max length\n");
                ret = -EFAULT;
                break;
            }
            sec->param.passphrase.ssid.ssid_len = strlen(end);
            strcpy((char *) sec->param.passphrase.ssid.ssid, end);
            PRINTM(INFO, "ssid=%s, len=%d\n", sec->param.passphrase.ssid.ssid,
                   (int) sec->param.passphrase.ssid.ssid_len);
        } else if (!strnicmp(opt, "bssid", strlen(opt))) {
            woal_mac2u8((t_u8 *) & sec->param.passphrase.bssid, end);
        } else if (!strnicmp(opt, "psk", strlen(opt)) &&
                   req->action == MLAN_ACT_SET) {
            if (strlen(end) != (MLAN_MAX_PMK_LENGTH * 2)) {
                PRINTM(ERROR, "Invalid PMK length\n");
                ret = -EINVAL;
                break;
            }
            woal_ascii2hex(sec->param.passphrase.psk.pmk.pmk, end,
                           MLAN_MAX_PMK_LENGTH * 2);
            sec->param.passphrase.psk_type = MLAN_PSK_PMK;
        } else if (!strnicmp(opt, "passphrase", strlen(opt)) &&
                   req->action == MLAN_ACT_SET) {
            if (strlen(end) < MLAN_MIN_PASSPHRASE_LENGTH ||
                strlen(end) > MLAN_MAX_PASSPHRASE_LENGTH) {
                PRINTM(ERROR, "Invalid length for passphrase\n");
                ret = -EINVAL;
                break;
            }
            sec->param.passphrase.psk_type = MLAN_PSK_PASSPHRASE;
            strcpy(sec->param.passphrase.psk.passphrase.passphrase, end);
            sec->param.passphrase.psk.passphrase.passphrase_len = strlen(end);
            PRINTM(INFO, "passphrase=%s, len=%d\n",
                   sec->param.passphrase.psk.passphrase.passphrase,
                   (int) sec->param.passphrase.psk.passphrase.passphrase_len);
        } else {
            PRINTM(ERROR, "Invalid option %s\n", opt);
            ret = -EINVAL;
            break;
        }
    }
    if (ret)
        goto done;

    if (action == 2)
        sec->param.passphrase.psk_type = MLAN_PSK_CLEAR;
    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }
    if (action == 0) {
        memset(buf, 0, sizeof(buf));
        if (sec->param.passphrase.ssid.ssid_len) {
            len += sprintf(buf + len, "ssid:");
            memcpy(buf + len, sec->param.passphrase.ssid.ssid,
                   sec->param.passphrase.ssid.ssid_len);
            len += sec->param.passphrase.ssid.ssid_len;
            len += sprintf(buf + len, " ");
        }
        if (memcmp(&sec->param.passphrase.bssid, zero_mac, sizeof(zero_mac))) {
            mac = (t_u8 *) & sec->param.passphrase.bssid;
            len += sprintf(buf + len, "bssid:");
            for (i = 0; i < ETH_ALEN - 1; ++i)
                len += sprintf(buf + len, "%02x:", mac[i]);
            len += sprintf(buf + len, "%02x ", mac[i]);
        }
        if (sec->param.passphrase.psk_type == MLAN_PSK_PMK) {
            len += sprintf(buf + len, "psk:");
            for (i = 0; i < MLAN_MAX_PMK_LENGTH; ++i)
                len +=
                    sprintf(buf + len, "%02x",
                            sec->param.passphrase.psk.pmk.pmk[i]);
            len += sprintf(buf + len, "\n");
        }
        if (sec->param.passphrase.psk_type == MLAN_PSK_PASSPHRASE) {
            len +=
                sprintf(buf + len, "passphrase:%s \n",
                        sec->param.passphrase.psk.passphrase.passphrase);
        }
        if (wrq->u.data.pointer) {
            if (copy_to_user(wrq->u.data.pointer, buf, len)) {
                PRINTM(ERROR, "Copy to user failed, len %d\n", len);
                ret = -EFAULT;
                goto done;
            }
            wrq->u.data.length = len;
        }

    }
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Get esupp mode
 *  
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *  
 *  @return         0 --success, otherwise fail
 */
static int
woal_get_esupp_mode(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_sec_cfg *sec = NULL;
    mlan_ioctl_req *req = NULL;

    ENTER();

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_sec_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    sec = (mlan_ds_sec_cfg *) req->pbuf;
    sec->sub_command = MLAN_OID_SEC_CFG_ESUPP_MODE;
    req->req_id = MLAN_IOCTL_SEC_CFG;
    req->action = MLAN_ACT_GET;

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    if (copy_to_user
        (wrq->u.data.pointer, (t_u8 *) & sec->param.esupp_mode,
         sizeof(int) * 3)) {
        ret = -EFAULT;
        goto done;
    }
    wrq->u.data.length = 3;
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/** AES key length */
#define AES_KEY_LEN 16
/**
 *  @brief Adhoc AES control 
 *
 *  @param priv	    A pointer to moal_private structure
 *  @param wrq	    A pointer to user data
 *
 *  @return 	    0 --success, otherwise fail
 */
static int
woal_adhoc_aes_ioctl(moal_private * priv, struct iwreq *wrq)
{
    static char buf[256];
    int ret = 0, action = -1, i;
    t_u8 key_ascii[32];
    t_u8 key_hex[16];
    t_u8 *tmp;
    mlan_bss_info bss_info;
    mlan_ds_sec_cfg *sec = NULL;
    mlan_ioctl_req *req = NULL;

    ENTER();

    memset(key_ascii, 0x00, sizeof(key_ascii));
    memset(key_hex, 0x00, sizeof(key_hex));

    /* Get current BSS information */
    memset(&bss_info, 0, sizeof(bss_info));
    woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);
    if (bss_info.bss_mode != MLAN_BSS_MODE_IBSS ||
        bss_info.media_connected == MTRUE) {
        PRINTM(ERROR, "STA is connected or not in IBSS mode.\n");
        ret = -EOPNOTSUPP;
        goto done;
    }

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_sec_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    if (wrq->u.data.length) {
        if (copy_from_user(buf, wrq->u.data.pointer, wrq->u.data.length)) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        buf[wrq->u.data.length] = '\0';

        if (wrq->u.data.length == 1) {
            /* Get Adhoc AES Key */
            req->req_id = MLAN_IOCTL_SEC_CFG;
            req->action = MLAN_ACT_GET;
            sec = (mlan_ds_sec_cfg *) req->pbuf;
            sec->sub_command = MLAN_OID_SEC_CFG_ENCRYPT_KEY;
            sec->param.encrypt_key.key_len = AES_KEY_LEN;
            sec->param.encrypt_key.key_index = 0x40000000;
            if (MLAN_STATUS_SUCCESS !=
                woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
                ret = -EFAULT;
                goto done;
            }

            memcpy(key_hex, sec->param.encrypt_key.key_material,
                   sizeof(key_hex));
            HEXDUMP("Adhoc AES Key (HEX)", key_hex, sizeof(key_hex));

            wrq->u.data.length = sizeof(key_ascii) + 1;

            tmp = key_ascii;
            for (i = 0; i < sizeof(key_hex); i++)
                tmp += sprintf((char *) tmp, "%02x", key_hex[i]);
        } else if (wrq->u.data.length >= 2) {
            /* Parse the buf to get the cmd_action */
            action = woal_atox(&buf[0]);
            if (action < 1 || action > 2) {
                PRINTM(ERROR, "Invalid action argument %d\n", action);
                ret = -EINVAL;
                goto done;
            }

            req->req_id = MLAN_IOCTL_SEC_CFG;
            req->action = MLAN_ACT_SET;
            sec = (mlan_ds_sec_cfg *) req->pbuf;
            sec->sub_command = MLAN_OID_SEC_CFG_ENCRYPT_KEY;

            if (action == 1) {
                /* Set Adhoc AES Key */
                memcpy(key_ascii, &buf[2], sizeof(key_ascii));
                woal_ascii2hex(key_hex, (char *) key_ascii, sizeof(key_hex));
                HEXDUMP("Adhoc AES Key (HEX)", key_hex, sizeof(key_hex));

                sec->param.encrypt_key.key_len = AES_KEY_LEN;
                sec->param.encrypt_key.key_index = 0x40000000;
                memcpy(sec->param.encrypt_key.key_material,
                       key_hex, sec->param.encrypt_key.key_len);

                if (MLAN_STATUS_SUCCESS !=
                    woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
                    ret = -EFAULT;
                    goto done;
                }
            } else if (action == 2) {
                /* Clear Adhoc AES Key */
                sec->param.encrypt_key.key_len = AES_KEY_LEN;
                sec->param.encrypt_key.key_index = 0x40000000;
                memset(sec->param.encrypt_key.key_material, 0,
                       sizeof(sec->param.encrypt_key.key_material));

                if (MLAN_STATUS_SUCCESS !=
                    woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
                    ret = -EFAULT;
                    goto done;
                }
            } else {
                PRINTM(ERROR, "Invalid argument\n");
                ret = -EINVAL;
                goto done;
            }
        }

        HEXDUMP("Adhoc AES Key (ASCII)", key_ascii, sizeof(key_ascii));
        wrq->u.data.length = sizeof(key_ascii);
        if (wrq->u.data.pointer) {
            if (copy_to_user(wrq->u.data.pointer, &key_ascii,
                             sizeof(key_ascii))) {
                PRINTM(ERROR, "copy_to_user failed\n");
                ret = -EFAULT;
                goto done;
            }
        }
    }

  done:
    if (req)
        kfree(req);

    LEAVE();
    return ret;
}

#ifdef MFG_CMD_SUPPORT
/** 
 *  @brief Manufacturing command ioctl function
 *   
 *  @param priv		A pointer to wlan_private structure
 *  @param wrq 		A pointer to iwreq structure
 *  @return    		0 --success, otherwise fail
 */
static int
woal_mfg_command(moal_private * priv, struct iwreq *wrq)
{
    HostCmd_Header cmd_header;
    int ret = 0;
    mlan_ds_misc_cfg *misc = NULL;
    mlan_ioctl_req *req = NULL;

    ENTER();

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    misc = (mlan_ds_misc_cfg *) req->pbuf;
    memset(&cmd_header, 0, sizeof(cmd_header));

    /* get MFG command header */
    if (copy_from_user
        (&cmd_header, wrq->u.data.pointer, sizeof(HostCmd_Header))) {
        PRINTM(ERROR, "copy from user failed: MFG command header\n");
        ret = -EFAULT;
        goto done;
    }
    misc->param.mfgcmd.len = cmd_header.size;

    PRINTM(INFO, "MFG command len = %lu\n", misc->param.mfgcmd.len);

    if (cmd_header.size > MLAN_SIZE_OF_CMD_BUFFER) {
        ret = -EINVAL;
        goto done;
    }

    /* get the whole command from user */
    if (copy_from_user
        (misc->param.mfgcmd.cmd, wrq->u.data.pointer, cmd_header.size)) {
        PRINTM(ERROR, "copy from user failed: MFG command\n");
        ret = -EFAULT;
        goto done;
    }
    misc->sub_command = MLAN_OID_MISC_MFG_CMD;
    req->req_id = MLAN_IOCTL_MISC_CFG;
    req->action = MLAN_ACT_SET;

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }
    if (copy_to_user
        (wrq->u.data.pointer, (t_u8 *) misc->param.mfgcmd.cmd,
         MIN(cmd_header.size, misc->param.mfgcmd.len))) {
        ret = -EFAULT;
        goto done;
    }
    wrq->u.data.length = MIN(cmd_header.size, misc->param.mfgcmd.len);
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}
#endif

/** 
 *  @brief host command ioctl function
 *   
 *  @param priv		A pointer to wlan_private structure
 *  @param wrq 		A pointer to iwreq structure
 *  @return    		0 --success, otherwise fail
 */
static int
woal_host_command(moal_private * priv, struct iwreq *wrq)
{
    HostCmd_Header cmd_header;
    int ret = 0;
    mlan_ds_misc_cfg *misc = NULL;
    mlan_ioctl_req *req = NULL;

    ENTER();

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    misc = (mlan_ds_misc_cfg *) req->pbuf;
    memset(&cmd_header, 0, sizeof(cmd_header));

    /* get command header */
    if (copy_from_user
        (&cmd_header, wrq->u.data.pointer, sizeof(HostCmd_Header))) {
        PRINTM(ERROR, "copy from user failed: Host command header\n");
        ret = -EFAULT;
        goto done;
    }
    misc->param.hostcmd.len = woal_le16_to_cpu(cmd_header.size);

    PRINTM(INFO, "Host command len = %lu\n", misc->param.hostcmd.len);

    if (woal_le16_to_cpu(cmd_header.size) > MLAN_SIZE_OF_CMD_BUFFER) {
        ret = -EINVAL;
        goto done;
    }

    /* get the whole command from user */
    if (copy_from_user
        (misc->param.hostcmd.cmd, wrq->u.data.pointer,
         woal_le16_to_cpu(cmd_header.size))) {
        PRINTM(ERROR, "copy from user failed\n");
        ret = -EFAULT;
        goto done;
    }
    misc->sub_command = MLAN_OID_MISC_HOST_CMD;
    req->req_id = MLAN_IOCTL_MISC_CFG;

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }
    if (copy_to_user
        (wrq->u.data.pointer, (t_u8 *) misc->param.hostcmd.cmd,
         misc->param.hostcmd.len)) {
        ret = -EFAULT;
        goto done;
    }
    wrq->u.data.length = misc->param.hostcmd.len;
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/** 
 *  @brief arpfilter ioctl function
 *   
 *  @param priv		A pointer to wlan_private structure
 *  @param wrq 		A pointer to iwreq structure
 *  @return    		0 --success, otherwise fail
 */
static int
woal_arp_filter(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ds_misc_cfg *misc = NULL;
    mlan_ioctl_req *req = NULL;

    ENTER();

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    misc = (mlan_ds_misc_cfg *) req->pbuf;
    misc->sub_command = MLAN_OID_MISC_GEN_IE;
    req->req_id = MLAN_IOCTL_MISC_CFG;
    req->action = MLAN_ACT_SET;
    misc->param.gen_ie.type = MLAN_IE_TYPE_ARP_FILTER;
    misc->param.gen_ie.len = wrq->u.data.length;

    /* get the whole command from user */
    if (copy_from_user
        (misc->param.gen_ie.ie_data, wrq->u.data.pointer, wrq->u.data.length)) {
        PRINTM(ERROR, "copy from user failed\n");
        ret = -EFAULT;
        goto done;
    }

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Create a brief scan resp to relay basic BSS info to the app layer
 *
 *  When the beacon/probe response has not been buffered, use the saved BSS
 *    information available to provide a minimum response for the application
 *    ioctl retrieval routines.  Include:
 *        - Timestamp
 *        - Beacon Period
 *        - Capabilities (including WMM Element if available)
 *        - SSID
 *
 *  @param ppbuffer  Output parameter: Buffer used to create basic scan rsp
 *  @param pbss_desc Pointer to a BSS entry in the scan table to create
 *                   scan response from for delivery to the application layer
 *
 *  @return          void
 */
static void
wlan_scan_create_brief_table_entry(t_u8 ** ppbuffer,
                                   BSSDescriptor_t * pbss_desc)
{
    t_u8 *ptmp_buf = *ppbuffer;
    t_u8 tmp_ssid_hdr[2];
    t_u8 ie_len = 0;

    if (copy_to_user(ptmp_buf, pbss_desc->time_stamp,
                     sizeof(pbss_desc->time_stamp))) {
        PRINTM(INFO, "Copy to user failed\n");
        return;
    }
    ptmp_buf += sizeof(pbss_desc->time_stamp);

    if (copy_to_user(ptmp_buf, &pbss_desc->beacon_period,
                     sizeof(pbss_desc->beacon_period))) {
        PRINTM(INFO, "Copy to user failed\n");
        return;
    }
    ptmp_buf += sizeof(pbss_desc->beacon_period);

    if (copy_to_user
        (ptmp_buf, &pbss_desc->cap_info, sizeof(pbss_desc->cap_info))) {
        PRINTM(INFO, "Copy to user failed\n");
        return;
    }
    ptmp_buf += sizeof(pbss_desc->cap_info);

    tmp_ssid_hdr[0] = 0;        /* Element ID for SSID is zero */
    tmp_ssid_hdr[1] = pbss_desc->ssid.ssid_len;
    if (copy_to_user(ptmp_buf, tmp_ssid_hdr, sizeof(tmp_ssid_hdr))) {
        PRINTM(INFO, "Copy to user failed\n");
        return;
    }
    ptmp_buf += sizeof(tmp_ssid_hdr);

    if (copy_to_user(ptmp_buf, pbss_desc->ssid.ssid, pbss_desc->ssid.ssid_len)) {
        PRINTM(INFO, "Copy to user failed\n");
        return;
    }
    ptmp_buf += pbss_desc->ssid.ssid_len;

    if (pbss_desc->wmm_ie.vend_hdr.element_id == WMM_IE) {
        ie_len = sizeof(IEEEtypes_Header_t) + pbss_desc->wmm_ie.vend_hdr.len;
        if (copy_to_user(ptmp_buf, &pbss_desc->wmm_ie, ie_len)) {
            PRINTM(INFO, "Copy to user failed\n");
            return;
        }

        ptmp_buf += ie_len;
    }

    if (pbss_desc->pwpa_ie) {
        if ((*(pbss_desc->pwpa_ie)).vend_hdr.element_id == WPA_IE) {
            ie_len =
                sizeof(IEEEtypes_Header_t) +
                (*(pbss_desc->pwpa_ie)).vend_hdr.len;
            if (copy_to_user(ptmp_buf, pbss_desc->pwpa_ie, ie_len)) {
                PRINTM(INFO, "Copy to user failed\n");
                return;
            }
        }

        ptmp_buf += ie_len;
    }

    if (pbss_desc->prsn_ie) {
        if ((*(pbss_desc->prsn_ie)).ieee_hdr.element_id == RSN_IE) {
            ie_len =
                sizeof(IEEEtypes_Header_t) +
                (*(pbss_desc->prsn_ie)).ieee_hdr.len;
            if (copy_to_user(ptmp_buf, pbss_desc->prsn_ie, ie_len)) {
                PRINTM(INFO, "Copy to user failed\n");
                return;
            }
        }

        ptmp_buf += ie_len;
    }

    *ppbuffer = ptmp_buf;
}

/** 
 *  @brief Create a wlan_ioctl_get_scan_table_entry for a given BSS 
 *         Descriptor for inclusion in the ioctl response to the user space
 *         application.
 *
 *
 *  @param pbss_desc   Pointer to a BSS entry in the scan table to form
 *                     scan response from for delivery to the application layer
 *  @param ppbuffer    Output parameter: Buffer used to output scan return struct
 *  @param pspace_left Output parameter: Number of bytes available in the 
 *                     response buffer.
 *
 *  @return MLAN_STATUS_SUCCESS, or < 0 with IOCTL error code
 */
static int
wlan_get_scan_table_ret_entry(BSSDescriptor_t * pbss_desc,
                              t_u8 ** ppbuffer, int *pspace_left)
{
    wlan_ioctl_get_scan_table_entry *prsp_entry;
    wlan_ioctl_get_scan_table_entry tmp_rsp_entry;
    int space_needed;
    t_u8 *pcurrent;
    int variable_size;

    const int fixed_size = (sizeof(tmp_rsp_entry.fixed_field_length)
                            + sizeof(tmp_rsp_entry.fixed_fields)
                            + sizeof(tmp_rsp_entry.bss_info_length));

    ENTER();

    pcurrent = *ppbuffer;

    /* The variable size returned is the stored beacon size */
    variable_size = pbss_desc->beacon_buf_size;

    /* If we stored a beacon and its size was zero, set the variable size
       return value to the size of the brief scan response
       wlan_scan_create_brief_table_entry creates.  Also used if we are not
       configured to store beacons in the first place */
    if (!variable_size) {
        variable_size = pbss_desc->ssid.ssid_len + 2;
        variable_size += (sizeof(pbss_desc->beacon_period)
                          + sizeof(pbss_desc->time_stamp)
                          + sizeof(pbss_desc->cap_info));
        if (pbss_desc->wmm_ie.vend_hdr.element_id == WMM_IE) {
            variable_size += (sizeof(IEEEtypes_Header_t)
                              + pbss_desc->wmm_ie.vend_hdr.len);
        }

        if (pbss_desc->pwpa_ie) {
            if ((*(pbss_desc->pwpa_ie)).vend_hdr.element_id == WPA_IE) {
                variable_size += (sizeof(IEEEtypes_Header_t)
                                  + (*(pbss_desc->pwpa_ie)).vend_hdr.len);
            }
        }

        if (pbss_desc->prsn_ie) {
            if ((*(pbss_desc->prsn_ie)).ieee_hdr.element_id == RSN_IE) {
                variable_size += (sizeof(IEEEtypes_Header_t)
                                  + (*(pbss_desc->prsn_ie)).ieee_hdr.len);
            }
        }
    }

    space_needed = fixed_size + variable_size;

    PRINTM(INFO, "GetScanTable: need(%d), left(%d)\n",
           space_needed, *pspace_left);

    if (space_needed >= *pspace_left) {
        *pspace_left = 0;
        LEAVE();
        return -E2BIG;
    }

    *pspace_left -= space_needed;

    tmp_rsp_entry.fixed_field_length = sizeof(prsp_entry->fixed_fields);

    memcpy(tmp_rsp_entry.fixed_fields.bssid,
           pbss_desc->mac_address, sizeof(prsp_entry->fixed_fields.bssid));

    tmp_rsp_entry.fixed_fields.rssi = pbss_desc->rssi;
    tmp_rsp_entry.fixed_fields.channel = pbss_desc->channel;
    tmp_rsp_entry.fixed_fields.network_tsf = pbss_desc->network_tsf;
    tmp_rsp_entry.bss_info_length = variable_size;

    /* 
     *  Copy fixed fields to user space
     */
    if (copy_to_user(pcurrent, &tmp_rsp_entry, fixed_size)) {
        PRINTM(INFO, "Copy to user failed\n");
        LEAVE();
        return -EFAULT;
    }

    pcurrent += fixed_size;

    if (pbss_desc->pbeacon_buf) {
        /* 
         *  Copy variable length elements to user space
         */
        if (copy_to_user(pcurrent, pbss_desc->pbeacon_buf,
                         pbss_desc->beacon_buf_size)) {
            PRINTM(INFO, "Copy to user failed\n");
            LEAVE();
            return -EFAULT;
        }

        pcurrent += pbss_desc->beacon_buf_size;
    } else {
        wlan_scan_create_brief_table_entry(&pcurrent, pbss_desc);
    }

    *ppbuffer = pcurrent;

    LEAVE();

    return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Retrieve the scan response/beacon table
 *
 *  @param wrq          A pointer to iwreq structure
 *  @param scan_resp    A pointer to mlan_scan_resp structure
 *  @param scan_start   argument
 *
 *  @return             MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static int
moal_ret_get_scan_table_ioctl(struct iwreq *wrq, mlan_scan_resp * scan_resp,
                              t_u32 scan_start)
{
    pBSSDescriptor_t pbss_desc, scan_table;
    mlan_scan_resp *prsp_info;
    int ret_code;
    int ret_len;
    int space_left;
    t_u8 *pcurrent;
    t_u8 *pbuffer_end;
    t_u32 num_scans_done;

    ENTER();

    num_scans_done = 0;
    ret_code = MLAN_STATUS_SUCCESS;

    prsp_info = (mlan_scan_resp *) wrq->u.data.pointer;
    prsp_info->pscan_table =
        (t_u8 *) prsp_info + sizeof(prsp_info->num_in_scan_table);
    pcurrent = prsp_info->pscan_table;

    pbuffer_end = wrq->u.data.pointer + wrq->u.data.length - 1;
    space_left = pbuffer_end - pcurrent;
    scan_table = (BSSDescriptor_t *) (scan_resp->pscan_table);

    PRINTM(INFO, "GetScanTable: scan_start req = %ld\n", scan_start);
    PRINTM(INFO, "GetScanTable: length avail = %d\n", wrq->u.data.length);

    if (!scan_start) {
        PRINTM(INFO, "GetScanTable: get current BSS Descriptor\n");

        /* Use to get current association saved descriptor */
        pbss_desc = scan_table;

        ret_code = wlan_get_scan_table_ret_entry(pbss_desc,
                                                 &pcurrent, &space_left);

        if (ret_code == MLAN_STATUS_SUCCESS) {
            num_scans_done = 1;
        }
    } else {
        scan_start--;

        while (space_left
               && (scan_start + num_scans_done < scan_resp->num_in_scan_table)
               && (ret_code == MLAN_STATUS_SUCCESS)) {

            pbss_desc = (scan_table + (scan_start + num_scans_done));

            PRINTM(INFO, "GetScanTable: get current BSS Descriptor [%ld]\n",
                   scan_start + num_scans_done);

            ret_code = wlan_get_scan_table_ret_entry(pbss_desc,
                                                     &pcurrent, &space_left);

            if (ret_code == MLAN_STATUS_SUCCESS) {
                num_scans_done++;
            }
        }
    }

    prsp_info->num_in_scan_table = num_scans_done;
    ret_len = pcurrent - (t_u8 *) wrq->u.data.pointer;

    wrq->u.data.length = ret_len;

    /* Return ret_code (EFAULT or E2BIG) in the case where no scan results were 
       successfully encoded. */
    LEAVE();
    return (num_scans_done ? MLAN_STATUS_SUCCESS : ret_code);
}

/** 
 *  @brief Get scan table ioctl
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq 		A pointer to iwreq structure
 *
 *  @return         MLAN_STATUS_SUCCESS -- success, otherwise fail          
 */
static mlan_status
woal_get_scan_table_ioctl(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ioctl_req *req = NULL;
    mlan_ds_scan *scan = NULL;
    t_u32 scan_start;
    mlan_status status = MLAN_STATUS_SUCCESS;

    ENTER();

    /* Allocate an IOCTL request buffer */
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_scan));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    /* Fill request buffer */
    scan = (mlan_ds_scan *) req->pbuf;
    req->req_id = MLAN_IOCTL_SCAN;
    req->action = MLAN_ACT_GET;

    /* get the whole command from user */
    if (copy_from_user(&scan_start, wrq->u.data.pointer, sizeof(scan_start))) {
        PRINTM(ERROR, "copy from user failed\n");
        ret = -EFAULT;
        goto done;
    }
    if (scan_start) {
        scan->sub_command = MLAN_OID_SCAN_NORMAL;
    } else {
        scan->sub_command = MLAN_OID_SCAN_SPECIFIC_SSID;
    }
    /* Send IOCTL request to MLAN */
    status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
    if (status == MLAN_STATUS_SUCCESS) {
        status =
            moal_ret_get_scan_table_ioctl(wrq, &scan->param.scan_resp,
                                          scan_start);
    }
  done:
    if (req && (status != MLAN_STATUS_PENDING))
        kfree(req);
    LEAVE();
    return status;
}

/** 
 *  @brief Set user scan
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq 		A pointer to iwreq structure
 *
 *  @return         MLAN_STATUS_SUCCESS -- success, otherwise fail          
 */
static mlan_status
woal_set_user_scan_ioctl(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0;
    mlan_ioctl_req *req = NULL;
    mlan_ds_scan *scan = NULL;
    union iwreq_data wrqu;
    mlan_status status = MLAN_STATUS_SUCCESS;

    ENTER();

    /* Allocate an IOCTL request buffer */
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_scan) + wrq->u.data.length);
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    /* Fill request buffer */
    scan = (mlan_ds_scan *) req->pbuf;
    scan->sub_command = MLAN_OID_SCAN_USER_CONFIG;
    req->req_id = MLAN_IOCTL_SCAN;
    req->action = MLAN_ACT_SET;

    if (copy_from_user
        (scan->param.user_scan.scan_cfg_buf, wrq->u.data.pointer,
         wrq->u.data.length)) {
        PRINTM(INFO, "Copy from user failed\n");
        LEAVE();
        return -EFAULT;
    }

    /* Send IOCTL request to MLAN */
    status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
    if (status == MLAN_STATUS_SUCCESS) {
        memset(&wrqu, 0, sizeof(union iwreq_data));
        wireless_send_event(priv->netdev, SIOCGIWSCAN, &wrqu, NULL);
    }
  done:
    if (req && (status != MLAN_STATUS_PENDING))
        kfree(req);
    LEAVE();
    return status;
}

/** Maximum number of probes to send on each channel */
#define MAX_PROBES  10
/**
 * @brief Set/Get scan configuration parameters
 * 
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 * 
 * @return         0 --success, otherwise fail
 */
static int
woal_set_get_scan_cfg(moal_private * priv, struct iwreq *wrq)
{
    int data[6], ret = 0;
    mlan_ds_scan *scan = NULL;
    mlan_ioctl_req *req = NULL;

    ENTER();
    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_scan));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }
    if (wrq->u.data.length > 6) {
        ret = -EINVAL;
        goto done;
    }
    scan = (mlan_ds_scan *) req->pbuf;
    scan->sub_command = MLAN_OID_SCAN_CONFIG;
    req->req_id = MLAN_IOCTL_SCAN;
    memset(data, 0, sizeof(data));
    if (wrq->u.data.length) {
        if (copy_from_user
            (data, wrq->u.data.pointer, (wrq->u.data.length * sizeof(int)))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        if ((data[0] < 0) || (data[0] > MLAN_SCAN_TYPE_PASSIVE)) {
            PRINTM(ERROR, "Invalid argument for scan type\n");
            ret = -EINVAL;
            goto done;
        }
        if ((data[1] < 0) || (data[1] > MLAN_SCAN_MODE_ANY)) {
            PRINTM(ERROR, "Invalid argument for scan mode\n");
            ret = -EINVAL;
            goto done;
        }
        if ((data[2] < 0) || (data[2] > MAX_PROBES)) {
            PRINTM(ERROR, "Invalid argument for scan probes\n");
            ret = -EINVAL;
            goto done;
        }
        if (((data[3] < 0) || (data[3] > MRVDRV_MAX_ACTIVE_SCAN_CHAN_TIME)) ||
            ((data[4] < 0) || (data[4] > MRVDRV_MAX_ACTIVE_SCAN_CHAN_TIME)) ||
            ((data[5] < 0) || (data[5] > MRVDRV_MAX_PASSIVE_SCAN_CHAN_TIME))) {
            PRINTM(ERROR, "Invalid argument for scan time\n");
            ret = -EINVAL;
            goto done;
        }
        req->action = MLAN_ACT_SET;
        memcpy(&scan->param.scan_cfg, (t_u32 *) data, sizeof(data));
    } else
        req->action = MLAN_ACT_GET;
    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }
    if (!wrq->u.data.length) {
        memcpy(data, (int *) &scan->param.scan_cfg, sizeof(data));
        if (copy_to_user(wrq->u.data.pointer, data, sizeof(data))) {
            ret = -EFAULT;
            goto done;
        }
        wrq->u.data.length = sizeof(data) / sizeof(int);
    }
  done:
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/** VSIE configuration buffer length */
#define VSIE_MAX_CFG_LEN    (MLAN_MAX_VSIE_LEN - 2 + 3)
/** VSIE mask to remove the IE */
#define VSIE_MASK_DISABLE   0x00
/** VSIE Action : Get */
#define VSIE_ACTION_GET     0
/** VSIE Action : Add */
#define VSIE_ACTION_ADD     1
/** VSIE Action : Delete */
#define VSIE_ACTION_DELETE  2
/** 
 *  @brief Get/Add/Remove vendor specific IE
 *   
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *  @return             MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static int
woal_vsie_cfg_ioctl(moal_private * priv, struct iwreq *wrq)
{
    int ret = 0, user_data_len = 0, ie_len = 0;
    mlan_ds_misc_cfg *misc = NULL;
    mlan_ioctl_req *req = NULL;
    t_u8 *buf = NULL;

    ENTER();

    user_data_len = wrq->u.data.length;
    if (user_data_len < 2 || user_data_len == 3 ||
        user_data_len > VSIE_MAX_CFG_LEN) {
        PRINTM(ERROR, "Invalid argument number!\n");
        LEAVE();
        return -EINVAL;
    }
    if (!(buf = (t_u8 *) kmalloc(VSIE_MAX_CFG_LEN, GFP_ATOMIC))) {
        PRINTM(ERROR, "Cannot allocate buffer for command!\n");
        LEAVE();
        return -ENOMEM;
    }
    memset(buf, 0, VSIE_MAX_CFG_LEN);
    if (copy_from_user(buf, wrq->u.data.pointer, user_data_len)) {
        PRINTM(INFO, "Copy from user failed\n");
        ret = -EFAULT;
        goto done;
    }

    if ((buf[0] > VSIE_ACTION_DELETE) ||
        (buf[1] > MLAN_MAX_VSIE_NUM - 1) ||
        ((buf[0] == VSIE_ACTION_ADD) &&
         !(buf[2] &&
           buf[2] <=
           (MLAN_VSIE_MASK_SCAN | MLAN_VSIE_MASK_ASSOC |
            MLAN_VSIE_MASK_ADHOC)))) {
        PRINTM(ERROR, "Invalid argument!\n");
        ret = -EINVAL;
        goto done;
    }

    req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
    if (req == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    req->req_id = MLAN_IOCTL_MISC_CFG;
    misc = (mlan_ds_misc_cfg *) req->pbuf;
    misc->sub_command = MLAN_OID_MISC_VS_IE;

    misc->param.vsie.id = buf[1];
    misc->param.vsie.mask = VSIE_MASK_DISABLE;
    switch (buf[0]) {
    case VSIE_ACTION_GET:
        req->action = MLAN_ACT_GET;
        break;
    case VSIE_ACTION_ADD:
        ie_len = user_data_len - 3;
        misc->param.vsie.mask = buf[2];
        misc->param.vsie.ie[1] = ie_len;
        memcpy(&misc->param.vsie.ie[2], &buf[3], ie_len);
    case VSIE_ACTION_DELETE:
        /* Set with mask 0 is remove */
        req->action = MLAN_ACT_SET;
        break;
    default:
        break;
    }

    if (MLAN_STATUS_SUCCESS != woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT)) {
        ret = -EFAULT;
        goto done;
    }

    wrq->u.data.length = misc->param.vsie.ie[1];
    if (wrq->u.data.length) {
        if (copy_to_user
            (wrq->u.data.pointer, &misc->param.vsie.ie[2],
             wrq->u.data.length)) {
            PRINTM(INFO, "Copy to user failed\n");
            ret = -EFAULT;
            goto done;
        }
    }

  done:
    if (buf)
        kfree(buf);
    if (req)
        kfree(req);
    LEAVE();
    return ret;
}

/**
 *  @brief Set/Get auth type
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq	    A pointer to iwreq structure
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_auth_type(moal_private * priv, struct iwreq *wrq)
{
    int auth_type;
    t_u32 auth_mode;
    int ret = 0;

    ENTER();
    if (wrq->u.data.length == 0) {
        if (MLAN_STATUS_SUCCESS !=
            woal_get_auth_mode(priv, MOAL_IOCTL_WAIT, &auth_mode)) {
            ret = -EFAULT;
            goto done;
        }
        auth_type = auth_mode;
        if (copy_to_user(wrq->u.data.pointer, &auth_type, sizeof(auth_type))) {
            PRINTM(ERROR, "Copy to user failed\n");
            ret = -EFAULT;
            goto done;
        }
        wrq->u.data.length = 1;
    } else {
        if (copy_from_user(&auth_type, wrq->u.data.pointer, sizeof(auth_type))) {
            PRINTM(ERROR, "Copy from user failed\n");
            ret = -EFAULT;
            goto done;
        }
        PRINTM(INFO, "SET: auth_type %d\n", auth_type);
        if (((auth_type < MLAN_AUTH_MODE_OPEN) ||
             (auth_type > MLAN_AUTH_MODE_SHARED))
            && (auth_type != MLAN_AUTH_MODE_AUTO)) {
            ret = -EINVAL;
            goto done;
        }
        auth_mode = auth_type;
        if (MLAN_STATUS_SUCCESS !=
            woal_set_auth_mode(priv, MOAL_IOCTL_WAIT, auth_mode)) {
            ret = -EFAULT;
            goto done;
        }
    }
  done:
    LEAVE();
    return ret;
}

/********************************************************
		Global Functions
********************************************************/

/** 
 *  @brief Get version 
 *   
 *  @param handle 		A pointer to moal_handle structure
 *  @param version		A pointer to version buffer
 *  @param max_len		max length of version buffer
 *
 *  @return 	   		N/A
 */
void
woal_get_version(moal_handle * handle, char *version, int max_len)
{
    union
    {
        t_u32 l;
        t_u8 c[4];
    } ver;
    char fw_ver[32];

    ENTER();

    ver.l = handle->fw_release_number;
    sprintf(fw_ver, "%u.%u.%u.p%u", ver.c[2], ver.c[1], ver.c[0], ver.c[3]);

    snprintf(version, max_len, driver_version, fw_ver);

    LEAVE();
}

/**
 *  @brief ioctl function - entry point
 *
 *  @param dev		A pointer to net_device structure
 *  @param req	   	A pointer to ifreq structure
 *  @param cmd 		Command
 *
 *  @return          0 --success, otherwise fail
 */
int
woal_do_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
    moal_private *priv = (moal_private *) netdev_priv(dev);
    struct iwreq *wrq = (struct iwreq *) req;
    int ret = 0;

    ENTER();

    PRINTM(INFO, "woal_do_ioctl: ioctl cmd = 0x%x\n", cmd);
    switch (cmd) {
    case WOAL_SETONEINT_GETWORDCHAR:
        switch (wrq->u.data.flags) {
        case WOAL_VERSION:     /* Get driver version */
            ret = woal_get_driver_version(priv, req);
            break;
        case WOAL_VEREXT:      /* Get extended driver version */
            ret = woal_get_driver_verext(priv, req);
            break;
        default:
            ret = -EOPNOTSUPP;
            break;
        }
        break;
    case WOAL_SETNONE_GETNONE:
        switch (wrq->u.data.flags) {
        case WOAL_WARMRESET:
            ret = woal_warm_reset(priv);
            break;
        default:
            ret = -EOPNOTSUPP;
            break;
        }
        break;
    case WOAL_SETONEINT_GETONEINT:
        switch (wrq->u.data.flags) {
        case WOAL_SET_GET_TXRATE:
            ret = woal_set_get_txrate(priv, wrq);
            break;
        case WOAL_SET_GET_REGIONCODE:
            ret = woal_set_get_regioncode(priv, wrq);
            break;
        case WOAL_SET_RADIO:
            ret = woal_set_get_radio(priv, wrq);
            break;
        case WOAL_WMM_ENABLE:
            ret = woal_wmm_enable_ioctl(priv, wrq);
            break;
        case WOAL_11D_ENABLE:
            ret = woal_11d_enable_ioctl(priv, wrq);
            break;
        case WOAL_SET_GET_TX_RX_ANT:
            ret = woal_set_get_tx_rx_ant(priv, wrq);
            break;
        case WOAL_SET_GET_QOS_CFG:
            ret = woal_set_get_qos_cfg(priv, wrq);
            break;
#ifdef REASSOCIATION
        case WOAL_SET_GET_REASSOC:
            ret = woal_set_get_reassoc(priv, wrq);
            break;
#endif /* REASSOCIATION */
        case WOAL_TXBUF_CFG:
            ret = woal_txbuf_cfg(priv, wrq);
            break;
        case WOAL_SET_GET_WWS_CFG:
            ret = woal_wws_cfg(priv, wrq);
            break;
        case WOAL_AUTH_TYPE:
            ret = woal_auth_type(priv, wrq);
            break;
        }
        break;

    case WOAL_SET_GET_SIXTEEN_INT:
        switch ((int) wrq->u.data.flags) {
        case WOAL_TX_POWERCFG:
            ret = woal_tx_power_cfg(priv, wrq);
            break;
#ifdef DEBUG_LEVEL1
        case WOAL_DRV_DBG:
            ret = woal_drv_dbg(priv, wrq);
            break;
#endif
        case WOAL_BEACON_INTERVAL:
            ret = woal_beacon_interval(priv, wrq);
            break;
        case WOAL_ATIM_WINDOW:
            ret = woal_atim_window(priv, wrq);
            break;
        case WOAL_SIGNAL:
            ret = woal_get_signal(priv, wrq);
            break;
        case WOAL_HEART_BEAT:
            ret = woal_heart_beat_ioctl(priv, wrq);
            break;
        case WOAL_11N_TX_CFG:
            ret = woal_11n_tx_cfg(priv, wrq);
            break;
        case WOAL_11N_AMSDU_AGGR_CTRL:
            ret = woal_11n_amsdu_aggr_ctrl(priv, wrq);
            break;
        case WOAL_11N_HTCAP_CFG:
            ret = woal_11n_htcap_cfg(priv, wrq);
            break;
        case WOAL_PRIO_TBL:
            ret = woal_11n_prio_tbl(priv, wrq);
            break;
        case WOAL_ADDBA_UPDT:
            ret = woal_addba_para_updt(priv, wrq);
            break;
        case WOAL_ADDBA_REJECT:
            ret = woal_addba_reject(priv, wrq);
            break;
        case WOAL_HS_CFG:
            ret = woal_hs_cfg(priv, wrq, MTRUE);
            break;
        case WOAL_HS_SETPARA:
            ret = woal_hs_setpara(priv, wrq);
            break;
        case WOAL_REG_READ_WRITE:
            ret = woal_reg_read_write(priv, wrq);
            break;
        case WOAL_INACTIVITY_TIMEOUT_EXT:
            ret = woal_inactivity_timeout_ext(priv, wrq);
            break;
        case WOAL_BAND_CFG:
            ret = woal_band_cfg(priv, wrq);
            break;
        case WOAL_SCAN_CFG:
            ret = woal_set_get_scan_cfg(priv, wrq);
            break;
        }
        break;

    case WOALGETLOG:
        ret = woal_get_log(priv, wrq);
        break;
    case WOAL_SET_GET_256_CHAR:
        switch (wrq->u.data.flags) {
        case WOAL_PASSPHRASE:
            ret = woal_passphrase(priv, wrq);
            break;
        case WOAL_ADHOC_AES:
            ret = woal_adhoc_aes_ioctl(priv, wrq);
            break;

        }
        break;

    case WOAL_SETADDR_GETNONE:
        switch ((int) wrq->u.data.flags) {
        case WOAL_DEAUTH:
            ret = woal_deauth(priv, wrq);
            break;
        default:
            ret = -EINVAL;
            break;
        }
        break;

    case WOAL_SETNONE_GETTWELVE_CHAR:
        /* 
         * We've not used IW_PRIV_TYPE_FIXED so sub-ioctl number is
         * in flags of iwreq structure, otherwise it will be in
         * mode member of iwreq structure.
         */
        switch ((int) wrq->u.data.flags) {
        case WOAL_WPS_SESSION:
            ret = woal_wps_cfg_ioctl(priv, wrq);
            break;
        default:
            ret = -EINVAL;
            break;
        }
        break;
    case WOAL_SETNONE_GET_FOUR_INT:
        switch ((int) wrq->u.data.flags) {
        case WOAL_DATA_RATE:
            ret = woal_get_txrx_rate(priv, wrq);
            break;
        case WOAL_ESUPP_MODE:
            ret = woal_get_esupp_mode(priv, wrq);
            break;
        default:
            ret = -EINVAL;
            break;
        }
        break;

#ifdef MFG_CMD_SUPPORT
    case WOAL_MFG_CMD:
        PRINTM(INFO, "Entering the Manufacturing ioctl SIOCCFMFG\n");
        ret = woal_mfg_command(priv, wrq);
        PRINTM(INFO, "Manufacturing Ioctl %s\n", (ret) ? "failed" : "success");
        break;
#endif
    case WOAL_SET_GET_64_INT:
        switch ((int) wrq->u.data.flags) {
        case WOAL_ECL_SYS_CLOCK:
            ret = woal_ecl_sys_clock(priv, wrq);
            break;
        }
        break;

    case WOAL_HOST_CMD:
        ret = woal_host_command(priv, wrq);
        break;
    case WOAL_ARP_FILTER:
        ret = woal_arp_filter(priv, wrq);
        break;

    case WOAL_SET_INTS_GET_CHARS:
        switch ((int) wrq->u.data.flags) {
        case WOAL_READ_EEPROM:
            ret = woal_read_eeprom(priv, wrq);
            break;
        }
        break;
    case WOAL_SET_GET_2K_BYTES:
        switch ((int) wrq->u.data.flags) {
        case WOAL_SET_USER_SCAN:
            ret = woal_set_user_scan_ioctl(priv, wrq);
            break;
        case WOAL_GET_SCAN_TABLE:
            ret = woal_get_scan_table_ioctl(priv, wrq);
            break;
        case WOAL_VSIE_CFG:
            ret = woal_vsie_cfg_ioctl(priv, wrq);
            break;
        }
        break;

    default:
        ret = -EINVAL;
        break;
    }

    LEAVE();
    return ret;
}
