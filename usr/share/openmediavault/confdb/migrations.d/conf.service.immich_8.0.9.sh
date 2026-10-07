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
# Migration for openmediavault-immich 8.0.9:
#
# Add the <libraries> container node below <services><immich>. It holds
# the external library mounts (one <library> entry per mount) that are
# rendered into the Immich compose stack. Existing configurations do
# not have the node yet; fresh installations get it from the create.d
# script. No existing settings are changed.
########################################################################

if omv_config_exists "/config/services/immich" && \
		! omv_config_exists "/config/services/immich/libraries"; then
	omv_config_add_node "/config/services/immich" "libraries"
fi

exit 0
