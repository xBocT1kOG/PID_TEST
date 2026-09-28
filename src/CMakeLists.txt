# Declare the application source and its GPIO / LEDC driver dependency.
idf_component_register(SRCS "main.c" PRIV_REQUIRES driver)
