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
     * State file written by the ctl helper after a successful photo
     * library migration. It records where the data was moved from and
     * to, and which directory still has to be cleaned up once the
     * settings have been saved.
     */
    const MIGRATION_STATE = "/var/lib/openmediavault/immich-migration.json";

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
        $this->registerMethod("testDir");
        $this->registerMethod("migrate");
        $this->registerMethod("cleanupOld");
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

        // Moving the photo library to another directory is only allowed
        // after the data has been copied with the 'Migrate photo library'
        // button. That record is what makes the removal of the previous
        // data (scheduled below and executed right after the settings
        // have been saved) safe.
        $current = $this->getSettingsObject($context);
        $oldRef = (string)($current["uploadRef"] ?? "");
        $newRef = (string)($data["uploadRef"] ?? "");
        $state = $this->readMigrationState();
        if ($newRef !== $oldRef) {
            $migrated = (true === ($state["completed"] ?? false))
                && (($state["fromRef"] ?? "") === $oldRef)
                && (($state["toRef"] ?? "") === $newRef);
            if (!$migrated && $this->libraryHasData($oldRef)) {
                throw new \OMV\Exception(
                    "The photo library directory was changed without ".
                    "migrating the data first. Use 'Test directory' to ".
                    "validate the new folder and 'Migrate photo library' ".
                    "to copy the data, then save again."
                );
            }
            $state["pendingCleanup"] = $oldRef;
        } else {
            $state["pendingCleanup"] = "";
        }
        $this->writeMigrationState($state);

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
        return $this->execBgCtl([$op, $this->getDirRef($settings)]);
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

    /**
     * Validate the photo library directory that is currently selected in
     * the settings form. The value has not been saved yet, so it is
     * passed by the form instead of being read from the configuration.
     */
    public function testDir($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execBgCtl([
            "testdir", (string)($params["uploadRef"] ?? "")
        ]);
    }

    /**
     * Copy the photo library to the directory currently selected in the
     * settings form (not saved yet), verify the copy and start the stack
     * temporarily on the new directory so the user can check the result
     * before saving.
     */
    public function migrate($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execBgCtl([
            "migrate", (string)($params["uploadRef"] ?? "")
        ]);
    }

    /**
     * Remove the data of the previous photo library. Scheduled by set()
     * and executed as a background task by the Save button, so the user
     * sees the progress of the removal.
     */
    public function cleanupOld($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $state = $this->readMigrationState();
        $ref = (string)($state["pendingCleanup"] ?? "");
        $result = $this->execBgCtl(["cleanup", $ref]);
        if ("" !== $ref) {
            // Only clear the flag once the removal task has been started;
            // clearing it late keeps the operation repeatable.
            $state = $this->readMigrationState();
            $state["pendingCleanup"] = "";
            $this->writeMigrationState($state);
        }
        return $result;
    }

    /**
     * Read the migration state file written by the ctl helper.
     */
    private function readMigrationState(): array
    {
        if (!is_readable(self::MIGRATION_STATE)) {
            return [];
        }
        $state = json_decode(
            (string)file_get_contents(self::MIGRATION_STATE), true
        );
        return is_array($state) ? $state : [];
    }

    private function writeMigrationState(array $state): void
    {
        @file_put_contents(
            self::MIGRATION_STATE,
            json_encode($state, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE)
        );
    }

    /**
     * True when the given shared folder already holds Immich data. An
     * empty (or not yet initialised) folder may be switched to without
     * migrating, because there is nothing to preserve.
     */
    private function libraryHasData(string $ref): bool
    {
        if ("" === $ref) {
            return false;
        }
        $cmd = new \OMV\System\Process(self::CTL, ["hasdata", $ref]);
        $cmd->setRedirect2to1();
        $cmd->setQuiet(true);
        $output = [];
        $exitStatus = 0;
        $cmd->execute($output, $exitStatus);
        return (0 === $exitStatus);
    }

    /**
     * Run the ctl helper as a background task and return the task status
     * file name so the task dialog can poll the live output.
     */
    private function execBgCtl(array $args): string
    {
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
}
