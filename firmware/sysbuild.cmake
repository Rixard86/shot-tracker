string(REPLACE "\"" "" signing_key "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}")
if(signing_key MATCHES "root-ec-p256\\.pem$" OR NOT EXISTS "${signing_key}")
  message(FATAL_ERROR
    "ShotPuck images must be signed with the private OTA key, not MCUboot's public dev key. "
    "Pass -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\\\"<absolute path to ota-signing-key.pem>\\\" (quotes included).")
endif()
message(STATUS "ShotPuck signing key: ${signing_key}")
