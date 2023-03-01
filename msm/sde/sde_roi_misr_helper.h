// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _SDE_ROI_MISR_HELPER_H
#define _SDE_ROI_MISR_HELPER_H

#include "sde_encoder_phys.h"
#include "sde_crtc.h"
#include "sde_hw_roi_misr.h"

#if defined(CONFIG_DRM_SDE_ROI_MISR)

/**
 * sde_roi_misr_init - initialize roi misr related data
 * @sde_crtc: Pointer to sde crtc
 */
void sde_roi_misr_init(struct sde_crtc *sde_crtc);

/**
 * sde_roi_misr_cfg_set - copy config data of roi misr
 *		to kernel space, then store the config to sde crtc state
 * @state: Pointer to drm crtc state
 * @usr_ptr: Pointer to config data of user space
 *
 * Return 0 on success, -ERRNO if there was an error.
 */
int sde_roi_misr_cfg_set(struct drm_crtc_state *state,
		void __user *usr_ptr);

/**
 * sde_roi_misr_populate_roi_range - populate roi range info
 * @c_conn: Pointer to sde connector structure
 * @info: Pointer to sde_kms_info structure
 * @mode: Pointer to drm_display_mode structure
 * @mode_info: Pointer to msm_mode_info structure
 */
void sde_roi_misr_populate_roi_range(
		struct sde_connector *c_conn,
		struct sde_kms_info *info,
		struct drm_display_mode *mode,
		struct msm_mode_info *mode_info);

/**
 * sde_roi_misr_get_mode_info - get roi misr mode info
 * @connector: Pointer to drm connector structure
 * @drm_mode: Pointer to drm_display mode structure
 * @mode_info: Pointer to msm_mode_info structure
 * @misr_mode_info: Output parameter, pointer to
 *			sde_roi_misr_mode_info structure
 * @display: Pointer to private display structure
 *
 * Returns: Zero on success
 */
int sde_roi_misr_get_mode_info(struct drm_connector *connector,
		const struct drm_display_mode *drm_mode,
		struct msm_mode_info *mode_info,
		struct sde_roi_misr_mode_info *misr_mode_info,
		void *display);

/**
 * sde_roi_misr_check_rois - check roi misr config
 * @crtc: Pointer to drm crtc
 * @state: Pointer to drm crtc state
 *
 * Return 0 on success, -ERRNO if there was an error.
 */
int sde_roi_misr_check_rois(struct drm_crtc_state *state);

/**
 * sde_roi_misr_setup - Set up roi misr block
 * @crtc: Pointer to drm crtc
 */
void sde_roi_misr_setup(struct drm_crtc *crtc);

/**
 * sde_roi_misr_hw_reset - reset roi misr register values
 * @phys_enc: Pointer to physical encoder structure
 */
void sde_roi_misr_hw_reset(struct sde_encoder_phys *phys_enc);

/**
 * sde_roi_misr_setup_irq_hw_idx - setup irq hardware
 *		index for master physical encoder
 * @phys_enc: Pointer to physical encoder structure
 */
void sde_roi_misr_setup_irq_hw_idx(struct sde_encoder_phys *phys_enc);

/**
 * sde_roi_misr_irq_control - enable or disable all irqs
 *		of one misr block
 * @phys_enc: Pointer to physical encoder structure
 * @base_irq_idx: one roi misr's base irq table index
 * @roi_idx: the roi index of one misr
 * @enable: control to enable or disable one misr block irqs
 *
 * Return 0 on success, -ERRNO if there was an error.
 */
int sde_roi_misr_irq_control(struct sde_encoder_phys *phys_enc,
		int base_irq_idx, int roi_idx, bool enable);

#else

static inline
void sde_roi_misr_init(struct sde_crtc *sde_crtc)
{
}

static inline
int sde_roi_misr_cfg_set(struct drm_crtc_state *state,
		void __user *usr_ptr)
{
	return 0;
}

static inline
void sde_roi_misr_populate_roi_range(
		struct sde_connector *c_conn,
		struct sde_kms_info *info,
		struct drm_display_mode *mode,
		struct msm_mode_info *mode_info)
{
	return 0;
}

static inline
int sde_roi_misr_get_mode_info(struct drm_connector *connector,
		const struct drm_display_mode *drm_mode,
		struct msm_mode_info *mode_info,
		struct sde_roi_misr_mode_info *misr_mode_info,
		void *display)
{
	return 0;
}

static inline
int sde_roi_misr_check_rois(struct drm_crtc_state *state)
{
	return 0;
}

static inline
void sde_roi_misr_setup(struct drm_crtc *crtc)
{
}

static inline
void sde_roi_misr_hw_reset(struct sde_encoder_phys *phys_enc)
{
}

static inline
void sde_roi_misr_setup_irq_hw_idx(struct sde_encoder_phys *phys_enc)
{
}

static inline
int sde_roi_misr_irq_control(struct sde_encoder_phys *phys_enc,
		int base_irq_idx, int roi_idx, bool enable)
{
	return 0;
}

#endif
#endif /* _SDE_ROI_MISR_HELPER_H */
