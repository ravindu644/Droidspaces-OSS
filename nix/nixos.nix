{
  lib,
  inputs,
  self,
  systems,
  ...
}: {
  perSystem = {...}: {
    legacyPackages = {
      nixosDroidspacesTarballs = lib.genAttrs systems (system: {
        minimal =
          (inputs.nixpkgs.lib.nixosSystem {
            inherit system;
            modules = [self.nixosModules.working-droidspaces-rootfs-minimal];
          }).config.system.build.tarball;

        minimal-with-systemd-v259 =
          (inputs.nixpkgs-with-systemd-v259.lib.nixosSystem {
            inherit system;
            modules = [self.nixosModules.working-droidspaces-rootfs-minimal];
          }).config.system.build.tarball;

        minimal-with-systemd-v257 =
          (inputs.nixpkgs-with-systemd-v257.lib.nixosSystem {
            inherit system;
            modules = [self.nixosModules.working-droidspaces-rootfs-minimal];
          }).config.system.build.tarball;

        minimal-with-pinned-systemd-v257 =
          (inputs.nixpkgs.lib.nixosSystem {
            inherit system;
            modules = [
              self.nixosModules.working-droidspaces-rootfs-minimal
              self.nixosModules.pin-systemd-v257
              self.nixosModules.container-disable-unwanted
            ];
          }).config.system.build.tarball;
      });
    };
  };

  flake = {
    nixosModules = {
      # Minimal configuration that doesn't cause systemd degradation
      working-droidspaces-rootfs-minimal = {
        modulesPath,
        pkgs,
        ...
      }: {
        imports = [
          "${modulesPath}/virtualisation/lxc-container.nix"
        ];

        nixpkgs.overlays = [
          self.overlays.default
        ];

        # These services are broken or unnecessaray in a droidspaces container
        systemd.services.nix-channel-init.enable = false;
        systemd.services.wpa_supplicant.enable = false;
        systemd.services.systemd-networkd-wait-online.enable = false;
        systemd.sockets.systemd-journald-audit.enable = false;

        # Set iptables
        networking.firewall.package = pkgs.iptables-legacy;
        networking.nftables.enable = false;
        environment.systemPackages = [pkgs.iptables-legacy];
        # TODO: Fix firewall issue
        networking.firewall.enable = false;
        systemd.services.firewall.enable = false;

        # TODO: find out what breaks cellular network in host mode and find a solution for it
        systemd.network.enable = lib.mkDefault false;

        # Only let these start on nat mode
        systemd.services.NetworkManager.serviceConfig.ExecCondition = [
          (pkgs.droidspacesConfigIf "net_mode=nat")
        ];

        systemd.services.dhcpcd.serviceConfig.ExecCondition = [
          (pkgs.droidspacesConfigIf "net_mode=nat")
        ];

        systemd.services.systemd-resolved.serviceConfig.ExecCondition = [
          (pkgs.droidspacesConfigIf "net_mode=nat")
        ];

        networking.nameservers = ["8.8.8.8" "1.1.1.1"];

        # Restrict udev to Android-safe subsystems only (prevent coldplugging host hardware)
        systemd.services.systemd-udev-trigger.serviceConfig.ExecStart = lib.mkForce [
          ""
          "-udevadm trigger --subsystem-match=usb --subsystem-match=block --subsystem-match=input --subsystem-match=tty --subsystem-match=net"
        ];
        # Clear ConditionPathIsReadWrite= from upstream units
        systemd.services.systemd-udevd.unitConfig.ConditionPathIsReadWrite = lib.mkForce [];
        systemd.services.systemd-udev-trigger.unitConfig.ConditionPathIsReadWrite = lib.mkForce [];
        systemd.services.systemd-udev-settle.unitConfig.ConditionPathIsReadWrite = lib.mkForce [];
        systemd.sockets.systemd-udevd-kernel.unitConfig.ConditionPathIsReadWrite = lib.mkForce [];
        systemd.sockets.systemd-udevd-control.unitConfig.ConditionPathIsReadWrite = lib.mkForce [];

        # Prevents systemd from acting on the power button when running
        # on Android, where the power key is used to wake/sleep the device.
        services.logind.settings.Login = {
          HandlePowerKey = "ignore";
          HandleSuspendKey = "ignore";
          HandleHibernateKey = "ignore";
          HandlePowerKeyLongPress = "ignore";
          HandlePowerKeyLongPressHibernate = "ignore";
        };

        # Journald configuration (skip Audit, KMsg, etc)
        services.journald.extraConfig = ''
          ReadKMsg=no
          Audit=no
          Storage=volatile
          SystemMaxUse=50M
          RuntimeMaxUse=10M
          MaxRetentionSec=7day
          MaxLevelStore=info
        '';

        services.logrotate.settings.header.maxsize = "50M";

        systemd.network = {
          networks."10-eth-dhcp" = {
            matchConfig.Name = "eth*";
            networkConfig = {
              DHCP = "yes";
              IPv6AcceptRA = true;
            };
            dhcpV4Config = {
              UseDNS = true;
              UseDomains = true;
              RouteMetric = 100;
            };
          };
        };

        users.groups = {
          aid_inet = {gid = 3003;};
          aid_net_raw = {gid = 3004;};
          aid_net_admin = {gid = 3005;};
        };
        users.users.root.extraGroups = ["aid_inet" "aid_net_raw" "aid_net_admin" "input" "video" "tty"];

        nix.settings.experimental-features = ["nix-command" "flakes"];

        system.stateVersion = "26.05";
      };

      container-disable-unwanted = {
        security.auditd.enable = false;

        systemd.services."getty@".enable = false;
        systemd.services."autovt@".enable = false;

        systemd.oomd.enable = false;

        services.timesyncd.enable = false;
        services.chrony.enable = false;
        services.ntp.enable = false;

        services.thermald.enable = false;
        services.power-profiles-daemon.enable = false;
        services.upower.enable = false;

        services.fwupd.enable = false;
        services.smartd.enable = false;
        services.irqbalance.enable = false;
        services.udisks2.enable = false;
      };
    };
  };
}
