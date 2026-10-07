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

CONFIG="/etc/openmediavault/config.xml"

########################################################################
# Migration for openmediavault-immich 8.0.7:
#
# The plugin no longer renders its own stack files into a user selected
# shared folder. The Immich Docker Compose stack now follows the
# openmediavault-compose conventions and is registered in the Compose
# plugin (conf.service.compose.file): the stack files live in
#   <compose shared folder>/immich/immich.yml
#   <compose shared folder>/immich/immich.env
# and the stack is managed from the Compose web UI (Services ->
# Compose -> Files/Services) as well as from the Immich settings page.
#
# The now unused 'composeDirRef' property is removed. The previous
# stack (rendered into that folder with the fixed container names
# immich_server/immich_redis/immich_postgres/...) has to be stopped
# first, otherwise the newly named stack would clash with the existing
# container names. Photos and database are never touched.
########################################################################

if omv_config_exists "/config/services/immich/composeDirRef"; then
	oldref="$(xmlstarlet sel -t -v \
		"/config/services/immich/composeDirRef" "${CONFIG}" 2>/dev/null \
		| xmlstarlet unesc 2>/dev/null || true)"
	if [ -n "${oldref}" ]; then
		olddir="$(omv_get_sharedfolder_path "${oldref}" 2>/dev/null || true)"
		if [ -n "${olddir}" ] && [ -f "${olddir}/docker-compose.yml" ]; then
			# Stop the previous stack so its fixed container names are
			# released for the stack managed by the Compose plugin.
			(
				cd "${olddir}" && docker compose down --remove-orphans
			) >/dev/null 2>&1 || true
		fi
	fi
	omv_config_delete "/config/services/immich/composeDirRef"
fi

exit 0
