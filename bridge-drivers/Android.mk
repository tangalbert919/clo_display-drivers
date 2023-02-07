# Android makefile for display bridge kernel modules
DISPLAY_SELECT := CONFIG_DRM_MSM=m

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# This makefile is only for DLKM
ifneq ($(findstring vendor,$(LOCAL_PATH)),)

ifneq ($(findstring opensource,$(LOCAL_PATH)),)
	DISPLAY_BLD_DIR := $(TOP)/vendor/qcom/opensource/display-drivers
endif # opensource

DLKM_DIR := $(TOP)/device/qcom/common/dlkm

LOCAL_ADDITIONAL_DEPENDENCIES := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)

# List of board platforms for which below driver's API should not be enabled
NO_MMRM_BOARDS          := kona
NO_MSM_EXT_DISP_BOARDS  := taro bengal
NO_SEC_BOARDS           := taro bengal
NO_HW_FENCE_BOARDS      := taro bengal kona

# Build lt9611uxc.ko
###########################################################
# This is set once per LOCAL_PATH, not per (kernel) module
KBUILD_OPTIONS := DISPLAY_ROOT=$(DISPLAY_BLD_DIR)
KBUILD_OPTIONS += MODNAME=lt9611uxc
KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
KBUILD_OPTIONS += $(DISPLAY_SELECT)

ifneq ($(TARGET_BOARD_AUTO), true)
$(info Build the symbols respective to the platform)
ifneq ($(call is-board-platform-in-list, $(NO_MMRM_BOARDS)),true)
KBUILD_OPTIONS += KBUILD_EXTRA_SYMBOLS+=$(PWD)/$(call intermediates-dir-for,DLKM,mmrm-module-symvers)/Module.symvers
endif
ifneq ($(call is-board-platform-in-list, $(NO_MSM_EXT_DISP_BOARDS)),true)
KBUILD_OPTIONS += KBUILD_EXTRA_SYMBOLS+=$(PWD)/$(call intermediates-dir-for,DLKM,msm-ext-disp-module-symvers)/Module.symvers
endif
ifneq ($(call is-board-platform-in-list, $(NO_SEC_BOARDS)),true)
KBUILD_OPTIONS += KBUILD_EXTRA_SYMBOLS+=$(PWD)/$(call intermediates-dir-for,DLKM,sec-module-symvers)/Module.symvers
endif
ifneq ($(call is-board-platform-in-list, $(NO_HW_FENCE_BOARDS)),true)
KBUILD_OPTIONS += KBUILD_EXTRA_SYMBOLS+=$(PWD)/$(call intermediates-dir-for,DLKM,hw-fence-module-symvers)/Module.symvers
endif
endif

###########################################################
include $(CLEAR_VARS)
LOCAL_SRC_FILES   := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := lt9611uxc.ko
LOCAL_MODULE_KBUILD_NAME  := lt9611uxc.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)

ifneq ($(TARGET_BOARD_AUTO), true)
ifneq ($(call is-board-platform-in-list, $(NO_MMRM_BOARDS)),true)
LOCAL_REQUIRED_MODULES    += mmrm-module-symvers
LOCAL_ADDITIONAL_DEPENDENCIES += $(call intermediates-dir-for,DLKM,mmrm-module-symvers)/Module.symvers
endif
ifneq ($(call is-board-platform-in-list, $(NO_MSM_EXT_DISP_BOARDS)),true)
LOCAL_REQUIRED_MODULES    += msm-ext-disp-module-symvers
LOCAL_ADDITIONAL_DEPENDENCIES += $(call intermediates-dir-for,DLKM,msm-ext-disp-module-symvers)/Module.symvers
endif
ifneq ($(call is-board-platform-in-list, $(NO_SEC_BOARDS)),true)
LOCAL_REQUIRED_MODULES    += sec-module-symvers
LOCAL_ADDITIONAL_DEPENDENCIES += $(call intermediates-dir-for,DLKM,sec-module-symvers)/Module.symvers
endif
ifneq ($(call is-board-platform-in-list, $(NO_HW_FENCE_BOARDS)),true)
LOCAL_REQUIRED_MODULES    += hw-fence-module-symvers
LOCAL_ADDITIONAL_DEPENDENCIES += $(call intermediates-dir-for,DLKM,hw-fence-module-symvers)/Module.symvers
endif
endif

include $(DLKM_DIR)/Build_external_kernelmodule.mk
###########################################################
endif # DLKM check
