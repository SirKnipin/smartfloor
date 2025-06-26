#include "pid_controller.h"

void pid_init(pid_controller_t *pid, float kp, float ki, float kd, float min_output, float max_output) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->previous_error = 0;
    pid->integral = 0;
    pid->output_min = min_output;
    pid->output_max = max_output;
}

float pid_compute(pid_controller_t *pid, float setpoint, float measured_value) {
    float error = setpoint - measured_value;
    pid->integral += error;
    float derivative = error - pid->previous_error;
    float output = pid->kp * error + pid->ki * pid->integral + pid->kd * derivative;

    // Saturation filter
    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;

    pid->previous_error = error;
    return output;
}
