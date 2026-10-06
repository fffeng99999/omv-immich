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

namespace Engined\Module;

/**
 * Immich engine module.
 *
 * Marks the module dirty whenever the settings change so that
 * 'Apply changes' re-runs the Salt state (.env / docker-compose.yml
 * rendering), and reports the service status to the global Services
 * monitoring (dashboard).
 */
class Immich extends \OMV\Engine\Module\ServiceAbstract
	implements \OMV\Engine\Notify\IListener, \OMV\Engine\Module\IServiceStatus
{
	public function getName()
	{
		return "immich";
	}

	public function getStatus()
	{
		$db = \OMV\Config\Database::getInstance();
		$object = $db->get("conf.service.immich");
		$cmd = new \OMV\System\Process("/usr/sbin/omv-immich-ctl", [
			"status", (string)$object->get("composeDirRef")
		]);
		$cmd->setRedirect2to1();
		$cmd->setQuiet(true);
		$output = [];
		$exitStatus = 0;
		$cmd->execute($output, $exitStatus);
		$running = (trim(implode("\n", $output)) === "running");
		return [
			"name" => $this->getName(),
			"title" => gettext("Immich"),
			"enabled" => $object->get("enable"),
			"running" => $running
		];
	}

	public function bindListeners(\OMV\Engine\Notify\Dispatcher $dispatcher)
	{
		$dispatcher->addListener(
			OMV_NOTIFY_MODIFY,
			"org.openmediavault.conf.service.immich",
			[$this, "setDirty"]
		);
		// The rendered files depend on the resolved shared folder
		// paths, so a modified shared folder must mark the service
		// dirty when it is one of the referenced ones.
		$dispatcher->addListener(
			OMV_NOTIFY_MODIFY,
			"org.openmediavault.conf.system.sharedfolder",
			[$this, "onSharedFolder"]
		);
	}

	public function onSharedFolder($object)
	{
		$db = \OMV\Config\Database::getInstance();
		foreach (["composeDirRef", "uploadRef", "dbRef"] as $prop) {
			if ($db->exists("conf.service.immich", [
				"operator" => "stringEquals",
				"arg0" => $prop,
				"arg1" => $object["uuid"]
			])) {
				$this->setDirty();
				return;
			}
		}
	}
}
