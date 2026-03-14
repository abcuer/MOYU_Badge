idf_component_register(
    SRCS "imu/imu.c"
    INCLUDE_DIRS "." "imu"
    REQUIRES mpu6050 delay 
)