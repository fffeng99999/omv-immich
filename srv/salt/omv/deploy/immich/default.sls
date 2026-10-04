# This file is part of OpenMediaVault.
#
# @license   https://www.gnu.org/licenses/gpl.html GPL Version 3
# @author    ${GITHUB_USER}
#
# OpenMediaVault is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.

# Renders the Immich Docker Compose stack (.env + docker-compose.yml)
# into the configured compose directory.
#
# The stack itself is managed through 'docker compose' (plugin start/
# stop/restart/upgrade buttons run /usr/sbin/omv-immich-ctl as a
# background task with live output). On Apply, the stack is brought up
# automatically when it is enabled and the rendered files changed;
# disabling the service removes the containers (all data is kept in the
# bind-mounted upload/database directories).

{% set config = salt['omv_conf.get']('conf.service.immich') %}
{% set dir = config.composeDir | default('/srv/docker/immich', true) %}
{% set upload = config.uploadLocation | default(dir ~ '/upload', true) %}
{% set dbdir = config.dbDataLocation | default(dir ~ '/db', true) %}

render_immich_env:
  file.managed:
    - name: '{{ dir }}/.env'
    - source:
      - salt://{{ tpldir }}/files/immich.env.j2
    - template: jinja
    - context:
        config: {{ config | json }}
        dir: {{ dir | json }}
        upload: {{ upload | json }}
        dbdir: {{ dbdir | json }}
    - user: root
    - group: root
    - mode: '0600'
    - makedirs: True

render_immich_compose:
  file.managed:
    - name: '{{ dir }}/docker-compose.yml'
    - source:
      - salt://{{ tpldir }}/files/immich.compose.yml.j2
    - template: jinja
    - context:
        ml_enable: {{ config.mlEnable | to_bool | json }}
    - user: root
    - group: root
    - mode: '0644'
    - makedirs: True

{% if config.enable | to_bool %}

# Recreate the stack when the rendered files changed (settings change).
# The very first deployment (and explicit upgrades) are done with the
# plugin buttons so the image pull output is visible to the user.
immich_compose_up:
  cmd.run:
    - name: docker compose up -d --remove-orphans
    - cwd: '{{ dir }}'
    - onchanges:
      - file: render_immich_env
      - file: render_immich_compose

{% else %}

# Disabled: remove the containers but never touch the data. Skipped
# when the stack was never rendered.
immich_compose_down:
  cmd.run:
    - name: docker compose down --remove-orphans
    - cwd: '{{ dir }}'
    - onlyif: test -f '{{ dir }}/docker-compose.yml'

{% endif %}
