sdb = './tmp/sdb.vdi'
sdc = './tmp/sdc.vdi'
sdd = './tmp/sdd.vdi'

Vagrant.configure("2") do |config|
  config.vm.define "ceph" do |ceph|
    ceph.vm.box = "bento/ubuntu-16.04"
    ceph.vm.hostname = "ceph"

    ceph.vm.network :private_network, ip: "192.168.42.100", netmask: "255.255.255.0"
    ceph.vm.network :private_network, ip: "192.168.43.100", netmask: "255.255.255.0"

    ceph.vm.provider "virtualbox" do | v |
      v.memory = 1024
      v.cpus = 1
      unless File.exist?(sdb)
        v.customize ['createhd', '--filename', sdb, '--size', 1 * 5120]
      end
      unless File.exist?(sdc)
        v.customize ['createhd', '--filename', sdc, '--size', 1 * 5120]
      end
      unless File.exist?(sdd)
        v.customize ['createhd', '--filename', sdd, '--size', 1 * 5120]
      end
        v.customize ['storageattach', :id, '--storagectl', 'SATA Controller', '--port', 1, '--device', 0, '--type', 'hdd', '--medium', sdb]
        v.customize ['storageattach', :id, '--storagectl', 'SATA Controller', '--port', 2, '--device', 0, '--type', 'hdd', '--medium', sdc]
        v.customize ['storageattach', :id, '--storagectl', 'SATA Controller', '--port', 3, '--device', 0, '--type', 'hdd', '--medium', sdd]

      # set promiscuous mode on eth2
      v.customize ["modifyvm", :id, "--nicpromisc3", "allow-all"]
      # set promiscuous mode on eth3
      v.customize ["modifyvm", :id, "--nicpromisc4", "allow-all"]
    end

    # libvirt specific
    ceph.vm.provider :libvirt do |libvirt, override|
      libvirt.memory = 2048
      libvirt.cpus = 1
      libvirt.storage :file, :size => '10G', :device => 'sdb', :allow_existing => false, :bus => 'sata'
      libvirt.storage :file, :size => '10G', :device => 'sdc', :allow_existing => false, :bus => 'sata'
      libvirt.storage :file, :size => '10G', :device => 'sdd', :allow_existing => false, :bus => 'sata'
    end

    ceph.vm.provision "shell", path: "scripts/bootstrap_ceph.sh"
  end

end
