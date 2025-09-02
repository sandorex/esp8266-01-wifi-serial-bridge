std::optional<SerialConfig> parse_serial_config(String config) {
    if (config == "5N1") {
        return SERIAL_5N1;
    } else if (config == "6N1") {
        return SERIAL_6N1;
    } else if (config == "7N1") {
        return SERIAL_7N1;
    } else if (config == "5N2") {
        return SERIAL_5N2;
    } else if (config == "6N2") {
        return SERIAL_6N2;
    } else if (config == "7N2") {
        return SERIAL_7N2;
    } else if (config == "8N2") {
        return SERIAL_8N2;
    } else if (config == "5E1") {
        return SERIAL_5E1;
    } else if (config == "6E1") {
        return SERIAL_6E1;
    } else if (config == "7E1") {
        return SERIAL_7E1;
    } else if (config == "8E1") {
        return SERIAL_8E1;
    } else if (config == "5E2") {
        return SERIAL_5E2;
    } else if (config == "6E2") {
        return SERIAL_6E2;
    } else if (config == "7E2") {
        return SERIAL_7E2;
    } else if (config == "8E2") {
        return SERIAL_8E2;
    } else if (config == "5O1") {
        return SERIAL_5O1;
    } else if (config == "6O1") {
        return SERIAL_6O1;
    } else if (config == "7O1") {
        return SERIAL_7O1;
    } else if (config == "8O1") {
        return SERIAL_8O1;
    } else if (config == "5O2") {
        return SERIAL_5O2;
    } else if (config == "6O2") {
        return SERIAL_6O2;
    } else if (config == "7O2") {
        return SERIAL_7O2;
    } else if (config == "8O2") {
        return SERIAL_8O2;
    } else if (config == "8N1") {
        return SERIAL_8N1;
    } else if (config == "8N2") {
        return SERIAL_8N2;
    }

    return {};
}

std::optional<String> serial_config_to_string(SerialConfig config) {
    if (config == SERIAL_5N1) {
        return "5N1";
    } else if (config == SERIAL_6N1) {
        return "6N1";
    } else if (config == SERIAL_7N1) {
        return "7N1";
    } else if (config == SERIAL_5N2) {
        return "5N2";
    } else if (config == SERIAL_6N2) {
        return "6N2";
    } else if (config == SERIAL_7N2) {
        return "7N2";
    } else if (config == SERIAL_8N2) {
        return "8N2";
    } else if (config == SERIAL_5E1) {
        return "5E1";
    } else if (config == SERIAL_6E1) {
        return "6E1";
    } else if (config == SERIAL_7E1) {
        return "7E1";
    } else if (config == SERIAL_8E1) {
        return "8E1";
    } else if (config == SERIAL_5E2) {
        return "5E2";
    } else if (config == SERIAL_6E2) {
        return "6E2";
    } else if (config == SERIAL_7E2) {
        return "7E2";
    } else if (config == SERIAL_8E2) {
        return "8E2";
    } else if (config == SERIAL_5O1) {
        return "5O1";
    } else if (config == SERIAL_6O1) {
        return "6O1";
    } else if (config == SERIAL_7O1) {
        return "7O1";
    } else if (config == SERIAL_8O1) {
        return "8O1";
    } else if (config == SERIAL_5O2) {
        return "5O2";
    } else if (config == SERIAL_6O2) {
        return "6O2";
    } else if (config == SERIAL_7O2) {
        return "7O2";
    } else if (config == SERIAL_8O2) {
        return "8O2";
    } else if (config == SERIAL_8N1) {
        return "8N1";
    } else if (config == SERIAL_8N2) {
        return "8N2";
    }

    return {};
}