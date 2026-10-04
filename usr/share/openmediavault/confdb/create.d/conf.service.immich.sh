#!/usr/bin/env dash
#
# This file is part of OpenMediaVault.
#
# @license   https://www.gnu.org/licenses/gpl.html GPL Version 3
# @author    ${GITHUB_USER}
#
# OpenMediaVault is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.

set -e

. /usr/share/openmediavault/scripts/helper-functions

########################################################################
# Update the configuration.
# <config>
#   <services>
#     <immich>
#       <enable>0|1</enable>
#       <version>release</version>
#       <port>2283</port>
#       <timeZone>Etc/UTC</timeZone>
#       <composeDir>/srv/docker/immich</composeDir>
#       <uploadLocation>/srv/docker/immich/upload</uploadLocation>
#       <dbDataLocation>/srv/docker/immich/db</dbDataLocation>
#       <dbPassword>...</dbPassword>
#       <dbUsername>postgres</dbUsername>
#       <dbDatabaseName>immich</dbDatabaseName>
#       <mlEnable>0|1</mlEnable>
#     </immich>
#   </services>
# </config>
########################################################################
if ! omv_config_exists "/config/services/immich"; then
	omv_config_add_node "/config/services" "immich"
	omv_config_add_key "/config/services/immich" "enable" "0"
	omv_config_add_key "/config/services/immich" "version" "release"
	omv_config_add_key "/config/services/immich" "port" "2283"
	omv_config_add_key "/config/services/immich" "timeZone" "Etc/UTC"
	omv_config_add_key "/config/services/immich" "composeDir" "/srv/docker/immich"
	omv_config_add_key "/config/services/immich" "uploadLocation" "/srv/docker/immich/upload"
	omv_config_add_key "/config/services/immich" "dbDataLocation" "/srv/docker/immich/db"
	# Generate a random Postgres password once (A-Za-z0-9 only, as
	# required by the upstream example.env: openssl rand -hex 16).
	omv_config_add_key "/config/services/immich" "dbPassword" \
		"$(openssl rand -hex 16 2>/dev/null || head -c 16 /dev/urandom | od -An -tx1 | tr -d ' \n')"
	omv_config_add_key "/config/services/immich" "dbUsername" "postgres"
	omv_config_add_key "/config/services/immich" "dbDatabaseName" "immich"
	omv_config_add_key "/config/services/immich" "mlEnable" "0"
fi

exit 0
