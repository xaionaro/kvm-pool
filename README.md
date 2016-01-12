Main repository: (https://devel.mephi.ru/dyokunev/kvm-pool)[https://devel.mephi.ru/dyokunev/kvm-pool]

```
NAME
       kvm-pool  -  utility  to  manage  KVM (Kernel Virtual Machine) instances as a pool for
       rdesktop.

SYNOPSIS
       kvm-pool [ ... ] -- [ kvm-arguments ]

DESCRIPTION
       kvm-pool creates a pool of virtual machines using KVM (via libvirt), listens specified
       port and proxies connections to instances of the pool.

OPTIONS
       This options can be passed as arguments or to be used in the configuration file.

       To disable numeric option set to zero:
                   =0

       To disable string option (for example path to file) set to empty string:
                   =

       Also  you  can  use  previously  set  values  while  setting  new  options.  Substring
       %option_name% will be substituted with previously set  value  of  option  option_name.
       (see CONFIGURATION FILE)

       To set kvm-arguments in config file use '--'. An example:
              -- = -net nic -net tap -boot n -m 512

       -c, --config-file path
              Sets path to the config file (see CONFIGURATION FILE).

              Default: "".

       -K, --config-group string
              Sets configuration group name (see CONFIGURATION FILE).

              Default: "default".

       -m, --min-vms number
              Minimal number of running virtual machines.

              Default: 0.

       -M, --max-vms number
              Maximal number of running virtual machines.

              Default: 64.

       -s, --min-spare number
              Minimal number of spare (idle) virtual machines.

              Default: 1.

       -S, --max-spare number
              Maximal number of spare (idle) virtual machines.

              Default: 8.

       -r, --kill-vm-on-disconnect [0|1]
              Kill  the  VM  if the client disconnected. It's to prevent getting session of a
              one user by another one.

              Default: 1.

       -L, --listen host:port
              Which port to listen for incoming rdesktop connections.

              Default: 0.0.0.0:3389.

CONFIGURATION FILE
       kvm-pool supports configuration file.

       By default kvm-pool tries to read next files (in specified order):
              ~/.kvm-pool.conf
              /etc/kvm-pool/kvm-pool.conf

       This may be overrided with option --config-file.

       kvm-pool reads only one configuration file. In other words, if option --config-file is
       not set and file ~/.kvm-pool.conf is accessible and parsable, kvm-pool will not try to
       open /etc/kvm-pool/kvm-pool.conf.  Command line options have  precedence  over  config
       file options.

       Configuration  file  is  parsed  with glib's g_key_file_* API. That means, that config
       should consits from groups of key-value lines as in the example:
              [default]
              max-vms   = 16
              min-spare = 2

              [laboratory]
              config-group-inherits = default
              --        = -net nic -net tap -boot n -m 512
              min-spare = 4

       In this example there're 2 groups are set - "default" and "laboratory".  And the group
       "laboratory" inherited setup of the group "default" except options "min-spare".

       By  default  kvm-pool  uses  group  with  name "default". The group name can be set by
       option --config-group.

EXAMPLES
       Getting a pool of virtual machines booted using PXE with 512MB RAM:
              kvm-pool -- -net nic -net tap -boot n -m 512

AUTHOR
       Dmitry Yu Okunev <dyokunev@ut.mephi.ru> 0x8E30679C

SEE ALSO
       kvm(1)
```
