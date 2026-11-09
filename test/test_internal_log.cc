#include "internal_log/internal_log.h"

int main() {
    LOG_ERROR("error", 1);
    LOG_DEBUG("debug", 2);
    LOG_INFO("info", 3);

}