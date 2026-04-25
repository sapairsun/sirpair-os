echo STEyM2RvcmlzCg== | base64 --decode | sudo -S dd if=./sirpair-kernel.img of=/dev/disk4 bs=8k status=progress
sync
diskutil eject /dev/disk4
