$(call inherit-product, device/zte/tulip/full_tulip.mk)

# Inherit some common AOKP stuff.
$(call inherit-product, vendor/aokp/configs/common_full_phone.mk)

PRODUCT_NAME := aokp_tulip
BOARD_VENDOR := zte
TARGET_VENDOR := zte
PRODUCT_DEVICE := tulip

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRODUCT_DEVICE="tulip" \
    PRODUCT_NAME="P852A12" \
    BUILD_FINGERPRINT="ZTE/P852A12/tulip:7.1.1/NMF26V/20170830.223936:user/release-keys" \
    PRIVATE_BUILD_DESC="P852A12-user 7.1.1 NMF26V 20170830.223936 release-keys"
