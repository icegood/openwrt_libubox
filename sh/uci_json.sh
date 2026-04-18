json_add_array_value() {
	json_add_string "" "$1"
}

json_add_uci_string() {
	local json_key="$1"
	local section="$2"
	local option="$3"
	local value

	config_get value "$section" "$option" ""
	[ -z "$value" ] || json_add_string "$json_key" "$value"
}

json_add_uci_boolean() {
	local json_key="$1"
	local section="$2"
	local option="$3"
	local value normalized
	local log_tag="${UCI_JSON_LOG_TAG:-uci_json}"

	config_get value "$section" "$option" ""
	[ -z "$value" ] && return 0

	case "$value" in
		1|on|true|yes) normalized="1" ;;
		0|off|false|no) normalized="0" ;;
		*)
			logger -t "$log_tag" "Ignoring invalid boolean value '$value' for ${section}.${option}"
			return 0
			;;
	esac

	json_add_boolean "$json_key" "$normalized"
}

json_add_uci_array() {
	local json_key="$1"
	local section="$2"
	local option="$3"
	local values

	config_get values "$section" "$option" ""
	[ -z "$values" ] && return 0

	json_add_array "$json_key"
	config_list_foreach "$section" "$option" json_add_array_value
	json_close_array
}
