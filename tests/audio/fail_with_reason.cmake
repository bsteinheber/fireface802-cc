# Fails with the message in the environment variable FF802_REASON.
# Used for ALSA/UCM tests that are required (FF802_REQUIRE_ALSA_TESTS) but
# whose prerequisites are missing.
message(FATAL_ERROR "$ENV{FF802_REASON}")
