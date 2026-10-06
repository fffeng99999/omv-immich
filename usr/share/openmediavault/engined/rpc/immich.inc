<?php

/**
 * This file is part of OpenMediaVault.
 *
 * @license   https://www.gnu.org/licenses/gpl.html GPL Version 3
 * @author    ${GITHUB_USER}
 *
 * OpenMediaVault is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * OpenMediaVault is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

namespace Engined\Rpc;

/**
 * Immich photo library RPC service.
 *
 * Settings (conf.service.immich) live in the OMV configuration
 * database. The Salt state /srv/salt/omv/deploy/immich renders .env and
 * docker-compose.yml into the configured compose directory on Apply;
 * the stack itself is managed through 'docker compose' via the
 * /usr/sbin/omv-immich-ctl helper (background tasks with live output).
 *
 * The plugin only manages the OMV-side integration (rendering,
 * lifecycle, status); Immich itself is never modified.
 */
class OMVRpcServiceImmich extends \OMV\Rpc\ServiceAbstract
{
    const CTL = "/usr/sbin/omv-immich-ctl";

    /**
     * Settings fields allowed to be persisted by set(). Everything else
     * sent by the form (read-only status fields) is stripped.
     */
    private $settingsFields = [
        "enable", "version", "port", "timeZone",
        "composeDirRef", "uploadRef", "dbRef",
        "dbPassword", "dbUsername", "dbDatabaseName", "mlEnable"
    ];

    public function getName()
    {
        return "Immich";
    }

    public function initialize()
    {
        $this->registerMethod("get");
        $this->registerMethod("set");
        $this->registerMethod("start");
        $this->registerMethod("stop");
        $this->registerMethod("restart");
        $this->registerMethod("upgrade");
        $this->registerMethod("getLog");
        $this->registerMethod("getShowConfig");
    }

    /**
     * Run omv-immich-ctl with the given arguments and return the trimmed
     * stdout. Throws on failure when $throwOnError is set.
     */
    private function runCtl(array $args, bool $throwOnError = true): array
    {
        $cmd = new \OMV\System\Process(self::CTL, $args);
        $cmd->setRedirect2to1();
        $cmd->setQuiet(true);
        $output = [];
        $exitStatus = 0;
        $cmd->execute($output, $exitStatus);
        if (0 !== $exitStatus && $throwOnError) {
            throw new \OMV\ExecException($cmd, $output, $exitStatus);
        }
        return [$exitStatus, $output];
    }

    private function getSettingsObject($context): array
    {
        return \OMV\Rpc\Rpc::call("Config", "get", [
            "id" => "conf.service.immich"
        ], $context);
    }

    /**
     * The storage properties hold shared folder references (UUIDs).
     * The ctl helper resolves them to real paths via the OMV helper
     * functions (omv_get_sharedfolder_path), so this RPC only passes
     * the reference through.
     */
    private function getDirRef(array $object): string
    {
        return (string)($object["composeDirRef"] ?? "");
    }

    /**
     * Format the raw /api/server/version JSON into "major.minor.patch".
     */
    private function parseVersion(array $output): string
    {
        $raw = trim(implode("\n", $output));
        $json = json_decode($raw, true);
        if (!is_array($json) || !isset($json["major"])) {
            return "";
        }
        $version = $json["major"].".".$json["minor"].".".$json["patch"];
        if (!empty($json["prerelease"])) {
            $version .= "-".$json["prerelease"];
        }
        return $version;
    }

    /**
     * Settings object enriched with read-only status information.
     */
    public function get($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);

        $object = $this->getSettingsObject($context);
        $dirRef = $this->getDirRef($object);

        // Stack state. The status text is returned as the English msgid;
        // the Workbench form page runs it through the 'translate' filter
        // (request.get.transform merges the transformed keys into the
        // response, so all other fields are preserved).
        list(, $statusOut) = $this->runCtl(["status", $dirRef], false);
        $state = trim(implode("\n", $statusOut));
        if (!in_array($state, ["running", "partial", "stopped", "notinstalled"])) {
            $state = "stopped";
        }
        $object["running"] = ("running" === $state);
        $object["statusText"] = [
            "running" => "Running",
            "partial" => "Partial",
            "stopped" => "Stopped",
            "notinstalled" => "Not installed"
        ][$state];

        list(, $versionOut) = $this->runCtl(["version", $dirRef], false);
        $runningVersion = $this->parseVersion($versionOut);
        $object["runningVersion"] = $runningVersion;

        list(, $latestOut) = $this->runCtl(["latest", $dirRef], false);
        $latestVersion = trim(implode("\n", $latestOut));
        $object["latestVersion"] = $latestVersion;

        // Update is available when the running version is older than
        // the latest upstream release tag (leading 'v' ignored). When
        // either side is unknown the check reports false.
        $updateAvailable = false;
        if (("" !== $runningVersion) && ("" !== $latestVersion)) {
            $updateAvailable = version_compare(
                $runningVersion, ltrim($latestVersion, "v"), "<"
            );
        }
        $object["updateAvailable"] = $updateAvailable;

        // Resolve the compose directory through the ctl helper (the
        // storage properties hold shared folder references). Empty
        // while no shared folder is selected.
        list(, $pathOut) = $this->runCtl(["path", $dirRef], false);
        $dir = trim(implode("\n", $pathOut));
        $object["composeFile"] = ("" !== $dir) ? $dir."/docker-compose.yml" : "";

        return $object;
    }

    public function set($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $data = array_intersect_key($params, array_flip($this->settingsFields));
        return \OMV\Rpc\Rpc::call("Config", "set", [
            "id" => "conf.service.immich",
            "data" => $data
        ], $context);
    }

    /**
     * Run a stack operation as a background task (the WebUI shows a
     * task dialog with the live 'docker compose' output). This keeps
     * the HTTP request short even when images have to be pulled first.
     */
    private function execCtlTask($context, string $op): string
    {
        $settings = $this->getSettingsObject($context);
        $args = [$op, $this->getDirRef($settings)];
        return $this->execBgProc(
            function ($bgStatusFilename, $bgOutputFilename) use ($args) {
                $cmd = new \OMV\System\Process(self::CTL, $args);
                $cmd->setRedirect2to1();
                if (0 !== ($exitStatus = $this->exec(
                    $cmd, $output, $bgOutputFilename
                ))) {
                    throw new \OMV\ExecException($cmd, $output, $exitStatus);
                }
                return $output;
            }
        );
    }

    public function start($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "up");
    }

    public function stop($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "down");
    }

    public function restart($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "restart");
    }

    public function upgrade($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "upgrade");
    }

    public function getLog($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "logs");
    }

    public function getShowConfig($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "showcompose");
    }
}
