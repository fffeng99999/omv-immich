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
     * The shared folder references managed by the plugin. Each of them
     * can be moved to another shared folder from the settings page.
     */
    const REF_PROPERTIES = ["composeDirRef", "uploadRef", "dbRef"];

    /**
     * State directory and request file used by the migration helpers.
     * The ctl helper writes one state file per property
     * (immich-migration-<property>.json); the RPC writes the request
     * file holding the target folders that are currently selected in
     * the settings form (not saved yet).
     */
    const STATE_DIR = "/var/lib/openmediavault";
    const REQUEST_FILE = "/var/lib/openmediavault/immich-migrate-request.json";

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

        // A shared folder reference may only be pointed to another
        // folder after the data has been copied with the 'Migrate data'
        // button. That record is what makes the removal of the previous
        // data (scheduled below and executed right after the settings
        // have been saved) safe. Properties that did not change are
        // left completely untouched.
        $current = $this->getSettingsObject($context);
        foreach (self::REF_PROPERTIES as $prop) {
            $oldRef = (string)($current[$prop] ?? "");
            $newRef = (string)($data[$prop] ?? "");
            $state = $this->readMigrationState($prop);
            if ($newRef === $oldRef) {
                // Clear a stale cleanup marker of an earlier migration.
                if (!empty($state)) {
                    $state["pendingCleanup"] = "";
                    $this->writeMigrationState($prop, $state);
                }
                continue;
            }
            $migrated = (true === ($state["completed"] ?? false))
                && (($state["fromRef"] ?? "") === $oldRef)
                && (($state["toRef"] ?? "") === $newRef);
            if (!$migrated && $this->folderHasData($prop, $oldRef)) {
                throw new \OMV\Exception(sprintf(
                    "The %s folder was changed without migrating its ".
                    "data first. Use 'Test directories' to validate the ".
                    "new folder and 'Migrate data' to copy it, then save ".
                    "again.", $this->refLabel($prop)
                ));
            }
            $state["pendingCleanup"] = $oldRef;
            $this->writeMigrationState($prop, $state);
        }

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
     * Validate the shared folders that are currently selected in the
     * settings form (not saved yet). Only the properties whose value
     * differs from the saved configuration are checked.
     */
    public function testDir($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $this->writeRequest($this->changedRefs($params, $context));
        return $this->execBgCtl(["testdirall"]);
    }

    /**
     * Copy every changed shared folder to the location selected in the
     * settings form (not saved yet), verify the copies and start the
     * stack temporarily on the new locations so the user can check the
     * result before saving.
     */
    public function migrate($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $this->writeRequest($this->changedRefs($params, $context));
        return $this->execBgCtl(["migrateall"]);
    }

    /**
     * Remove the data of the previously used shared folders. Scheduled
     * by set() and executed as a background task by the Save button, so
     * the user sees the progress of the removal.
     */
    public function cleanupOld($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        // The ctl helper clears the pending cleanup marker of a property
        // itself, but only after its data has really been removed, so a
        // failed removal can be repeated by saving again.
        return $this->execBgCtl(["cleanupall"]);
    }

    /**
     * The shared folder references selected in the form that differ from
     * the saved configuration, keyed by property name.
     */
    private function changedRefs(array $params, $context): array
    {
        $current = $this->getSettingsObject($context);
        $changed = [];
        foreach (self::REF_PROPERTIES as $prop) {
            $newRef = (string)($params[$prop] ?? "");
            $oldRef = (string)($current[$prop] ?? "");
            if (("" !== $newRef) && ($newRef !== $oldRef)) {
                $changed[$prop] = $newRef;
            }
        }
        return $changed;
    }

    private function writeRequest(array $refs): void
    {
        @file_put_contents(
            self::REQUEST_FILE,
            json_encode($refs, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE)
        );
    }

    private function refLabel(string $prop): string
    {
        $labels = [
            "composeDirRef" => "stack files",
            "uploadRef" => "photo library",
            "dbRef" => "database"
        ];
        return $labels[$prop] ?? $prop;
    }

    /**
     * Read the migration state file of a property written by the ctl
     * helper.
     */
    private function readMigrationState(string $prop): array
    {
        $file = self::STATE_DIR."/immich-migration-".$prop.".json";
        if (!is_readable($file)) {
            return [];
        }
        $state = json_decode((string)file_get_contents($file), true);
        return is_array($state) ? $state : [];
    }

    private function writeMigrationState(string $prop, array $state): void
    {
        @file_put_contents(
            self::STATE_DIR."/immich-migration-".$prop.".json",
            json_encode($state, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE)
        );
    }

    /**
     * True when the given shared folder already holds data that belongs
     * to the property. An empty (or not yet initialised) folder may be
     * switched to without migrating, because there is nothing to
     * preserve.
     */
    private function folderHasData(string $prop, string $ref): bool
    {
        if ("" === $ref) {
            return false;
        }
        $cmd = new \OMV\System\Process(self::CTL, ["hasdata", $prop, $ref]);
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
