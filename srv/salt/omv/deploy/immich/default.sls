# This file is part of OpenMediaVault.
#
# @license   https://www.gnu.org/licenses/gpl.html GPL Version 3
# @author    ${GITHUB_USER} <${GITHUB_USER}@users.noreply.github.com>
#
# OpenMediaVault is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.

# Manages the lifecycle of the Immich Docker Compose stack.
#
# The stack files themselves are rendered by the openmediavault-compose
# plugin (the Immich settings page registers the stack through the
# Compose RPC); this state only brings the stack up when the service is
# enabled and stops it when it is disabled. The stack is executed with
# the Compose plugin helper 'omv-compose-run', so exactly the same
# --file/--env-file arguments are used as from the Compose web UI.
#
# The data (photo library and database) is never touched here. When the
# Compose plugin is not configured yet, the whole state is skipped so
# that applying the configuration never fails.

{% set config = salt['omv_conf.get']('conf.service.immich') %}
{% set compose = salt['omv_conf.get']('conf.service.compose') %}
{% if compose.sharedfolderref | length > 0 %}

{% set sfpath = salt['omv_conf.get_sharedfolder_path'](compose.sharedfolderref).rstrip('/') %}
{% set stackfile = sfpath ~ '/immich/immich.yml' %}

{% if config.enable | to_bool %}

# Enabled: make sure the stack is running (idempotent; the containers are
# recreated automatically when the rendered stack files changed).
immich_stack_up:
  cmd.run:
    - name: /usr/sbin/omv-compose-run immich up -d
    - onlyif: test -f '{{ stackfile }}'

{% else %}

# Disabled: remove the containers but never touch the data. Skipped when
# the stack was never registered.
immich_stack_down:
  cmd.run:
    - name: /usr/sbin/omv-compose-run immich down --remove-orphans
    - onlyif: test -f '{{ stackfile }}'

{% endif %}

{% else %}

# The Compose plugin shared folder is not configured yet: nothing to do.
immich_compose_not_configured:
  test.configurable_test_state:
    - name: immich_compose_not_configured
    - changes: False
    - result: True
    - comment: "Configure the Compose plugin (Services -> Compose -> Settings) and select the shared folder for the compose files first."

{% endif %}
