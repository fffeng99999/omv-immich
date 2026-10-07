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
# Migration for openmediavault-immich 8.0.2:
# the plain path properties (composeDir / uploadLocation /
# dbDataLocation) are replaced by shared folder references
# (composeDirRef / uploadRef / dbRef). Existing installations keep
# their old path keys in config.xml (harmless orphans, ignored by the
# datamodel); the new reference keys start empty, so after upgrading
# the shared folders must be selected once on the settings page and
# the changes applied. Data is not moved automatically.
########################################################################

if omv_config_exists "/config/services/immich"; then
	if ! omv_config_exists "/config/services/immich/composeDirRef"; then
		omv_config_add_key "/config/services/immich" "composeDirRef" ""
	fi
	if ! omv_config_exists "/config/services/immich/uploadRef"; then
		omv_config_add_key "/config/services/immich" "uploadRef" ""
	fi
	if ! omv_config_exists "/config/services/immich/dbRef"; then
		omv_config_add_key "/config/services/immich" "dbRef" ""
	fi
fi

exit 0
