#!/usr/bin/env dash
#
# This file is part of OpenMediaVault.
#
# @license   https://www.gnu.org/licenses/gpl.html GPL Version 3
# @author    ${GITHUB_USER} <${GITHUB_USER}@users.noreply.github.com>
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
#       <version>v3.2.4</version>
#       <port>2283</port>
#       <timeZone>Etc/UTC</timeZone>
#       <uploadRef>uuid-or-empty</uploadRef>
#       <dbRef>uuid-or-empty</dbRef>
#       <dbPassword>...</dbPassword>
#       <dbUsername>postgres</dbUsername>
#       <dbDatabaseName>immich</dbDatabaseName>
#       <mlEnable>0|1</mlEnable>
#       <libraries></libraries>
#     </immich>
#   </services>
# </config>
########################################################################
if ! omv_config_exists "/config/services/immich"; then
	omv_config_add_node "/config/services" "immich"
	omv_config_add_key "/config/services/immich" "enable" "0"
	omv_config_add_key "/config/services/immich" "version" "v3.2.4"
	omv_config_add_key "/config/services/immich" "port" "2283"
	omv_config_add_key "/config/services/immich" "timeZone" "Etc/UTC"
	omv_config_add_key "/config/services/immich" "uploadRef" ""
	omv_config_add_key "/config/services/immich" "dbRef" ""
	# Generate a random Postgres password once (A-Za-z0-9 only, as
	# required by the upstream example.env: openssl rand -hex 16).
	omv_config_add_key "/config/services/immich" "dbPassword" \
		"$(openssl rand -hex 16 2>/dev/null || head -c 16 /dev/urandom | od -An -tx1 | tr -d ' \n')"
	omv_config_add_key "/config/services/immich" "dbUsername" "postgres"
	omv_config_add_key "/config/services/immich" "dbDatabaseName" "immich"
	omv_config_add_key "/config/services/immich" "mlEnable" "0"
fi

# The external library mounts live in a child container node that is
# created on demand. The guard also covers existing installations
# (see the 8.0.9 migration script).
if omv_config_exists "/config/services/immich" && \
		! omv_config_exists "/config/services/immich/libraries"; then
	omv_config_add_node "/config/services/immich" "libraries"
fi

exit 0
