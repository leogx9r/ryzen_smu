# Ryzen SMU

![Ryzen SMU Capabilities](pics/preview.jpg)

*Ryzen SMU* is a Linux kernel driver that exposes access to the SMU (System Management Unit) for
certain AMD Ryzen Processors.

The following processor code names are supported:

- Colfax
- Renoir
- Picasso
- Matisse
- Threadripper
- Castle Peak
- Raven Ridge
- Raven Ridge 2
- Summit Ridge
- Pinnacle Ridge
- Rembrant
- Vermeer
- Vangogh
- Cezanne
- Milan

In addition, for the following models, the PM table can also be accessed:

- Matisse
- Renoir
- Picasso
- Raven Ridge 2

When loaded, the driver exposes several files under sysfs which can only be read with root
permissions (for obvious reasons):

- `/sys/kernel/ryzen_smu_drv/version`
- `/sys/kernel/ryzen_smu_drv/mp1_if_version`
- `/sys/kernel/ryzen_smu_drv/codename`
- `/sys/kernel/ryzen_smu_drv/smu_args`
- `/sys/kernel/ryzen_smu_drv/mp1_smu_cmd`
- `/sys/kernel/ryzen_smu_drv/smn`
- `/sys/kernel/ryzen_smu_drv/rsmu_cmd` (Not present on `Rembrant`, `Vangogh`, `Cezanne` and `Milan`)

For supported PM table models where RSMU is also supported, the following files are additionally 
exposed:

- `/sys/kernel/ryzen_smu_drv/pm_table_version`
- `/sys/kernel/ryzen_smu_drv/pm_table_size`
- `/sys/kernel/ryzen_smu_drv/pm_table`


## Installation

The kernel module may be installed either by DKMS or manually building and inserting the module.

### Ubuntu / Debian

```
sudo apt install dkms git build-essential linux-headers-$(uname -r)
git clone https://gitlab.com/leogx9r/ryzen_smu.git
cd ryzen_smu

sudo make dkms-install
```

### Stand-alone Installation

The module may be built and inserted into the running kernel manually as follows:

```
git clone https://gitlab.com/leogx9r/ryzen_smu.git
cd ryzen_smu

make
sudo insmod ryzen_smu.ko
```

## Confirming Module Works

Upon loading the module, you should see output in your `dmesg` window listing the SMU version:

```
# dmesg

...
[1091.154385] ryzen_smu: SMU v46.54.0
...
```

After which you can verify the existence of the sysfs files and attempt to read them:

```
# ls -lah /sys/kernel/ryzen_smu_drv
total 0
drwxr-xr-x  2 root root    0 May  7 03:01 ./
drwxr-xr-x 14 root root    0 May  7 03:01 ../
-r--------  1 root root 4.0K May  7 03:12 codename
-r--------  1 root root 4.0K May  7 03:10 mp1_if_version
-rw-------  1 root root 4.0K May  7 03:10 mp1_smu_cmd
-r--------  1 root root 4.0K May  7 03:12 pm_table
-r--------  1 root root 4.0K May  7 03:12 pm_table_size
-r--------  1 root root 4.0K May  7 03:12 pm_table_version
-rw-------  1 root root 4.0K May  7 03:10 rsmu_cmd
-rw-------  1 root root 4.0K May  7 03:10 smn
-rw-------  1 root root 4.0K May  7 03:10 smu_args
-r--------  1 root root 4.0K May  7 03:01 version

# cat /sys/kernel/ryzen_smu_drv/version          
SMU v46.54.0

# cat /sys/kernel/ryzen_smu_drv/mp1_if_version
2

# cat /sys/kernel/ryzen_smu_drv/codename
4

```

Following which, you can run the [test.py script](scripts/test.py) to verify that SMU and SMN
functionality is working:

```
# python3 scripts/test.py
Retrieved SMU Version: v46.54.0
Processor Code Name: Matisse
PM Table: [Supported/Unsupported]
SMN Offset[0x50200]: 0x00001539

Everything seems to be working properly!

```

## Explaining Sysfs Files

#### `/sys/kernel/ryzen_smu_drv/version`

Lists the current SMU firmware version in relation to the currently installed
[AGESA](https://en.wikipedia.org/wiki/AGESA).

The following are several lists of SMU to AGESA versions for Matisse on the ComboPiAM4
(Pre-X570/B550):

| SMU Version   | AGESA Version |
|:-------------:|:-------------:|
| 46.54         | 1.0.0.4 B     |
| 46.53         | 1.0.0.4       |
| 46.49         | 1.0.0.3 ABBA  |
| 46.37         | 1.0.0.3 A     |
| 43.18         | 1.0.0.2 A     |

Note: This file returns a string encoded version represented by the "SMU Version" above.

#### `/sys/kernel/ryzen_smu_drv/mp1_if_version`

Lists the interface version for the MP1 mailbox.

This can range from v9 to v13 and is indicated by the following table:

| Value | Interface Version |
|:-----:|:-----------------:|
| 0     | v9                |
| 1     | v10               |
| 2     | v11               |
| 3     | v12               |
| 4     | v13               |
| 5     | Undefined         |

Note: This file returns a string representation of the "Value" field above.

#### `/sys/kernel/ryzen_smu_drv/codename`

Returns a numeric index containing the running processor's codename based on the following
enumeration:

| Hex | Decimal | Code Name      |
|:---:|:-------:|:--------------:|
| 00h | 0       | Unknown        |
| 01h | 1       | Colfax         |
| 02h | 2       | Renoir         |
| 03h | 3       | Picasso        |
| 04h | 4       | Matisse        |
| 05h | 5       | Threadripper   |
| 06h | 6       | Castle Peak    |
| 07h | 7       | Raven Ridge    |
| 08h | 8       | Raven Ridge 2  |
| 09h | 9       | Summit Ridge   |
| 0Ah | 10      | Pinnacle Ridge |
| 0Bh | 11      | Rembrant       |
| 0Ch | 12      | Vermeer        |
| 0Dh | 13      | Vangogh        |
| 0Eh | 14      | Cezanne        |
| 0Fh | 15      | Milan          |

Note: This file returns 2 characters of the 'Decimal' encoded index.

#### `/sys/kernel/ryzen_smu_drv/rsmu_cmd` or `/sys/kernel/ryzen_smu_drv/mp1_smu_cmd`

This file allows the user to initiate an RSMU or MP1 SMU request. It accepts either an 8-bit or
32-bit command ID that is platform-dependent.

When this file is read, it produces the result on the status of the operation, as a 32 bit
little-endian encoded value:

| Hex | Decimal | Explanation                   |
|:---:|:-------:|:-----------------------------:|
| 00h | 0       | WAITING                       |
| 01h | 1       | OK                            |
| FFh | 255     | FAILED                        |
| FEh | 254     | UNKNOWN COMMAND               |
| FDh | 253     | REJECTED - PREREQUISITE UNMET |
| FCh | 252     | REJECTED - BUSY               |
| FBh | 251     | COMMAND TIMEOUT               |
| FAh | 250     | INVALID ARGUMENT              |
| F9h | 249     | UNSUPPORTED PLATFORM          |

#### `/sys/kernel/ryzen_smu_drv/smu_args`

When written to, this file accepts 6x 32-bit words (a total of 192 bits) that specify the arguments
used when executing an SMU command.

When read from, it lists either:

- The last values that were written to it before an SMU request was initiated
- The responses from the SMU after a request was completed

Note: All values sent to and read from this file must be in 6x 32-bit words encoded in little-endian
order, arguments numbered from 1 to 6.

#### `/sys/kernel/ryzen_smu_drv/smn`

Allows reading and writing 32 bit values from the SMN address space. To perform an operation, write
a value then read the file for the result.

The amount of bytes written indicates the operation performed:

| Bits Written | Operation | Action Taken                                                                 |
|:------------:|:---------:| ---------------------------------------------------------------------------- |
| 32           | Read      | Reads 32 bit address and returns the result                                  |
| 64           | Write     | Writes the second 32-bit value to the address specified by the first 32 bits |

Note: All values sent to and read from the device must are in little-endian binary format.

#### `/sys/kernel/ryzen_smu_drv/pm_table_size`

On supported platforms, this lists the maximum size of the `/sys/kernel/ryzen_smu_drv/pm_table`
file, in bytes.

Note: File is a 64 bit word encoded in little-endian binary order.

#### `/sys/kernel/ryzen_smu_drv/pm_table_version`

On supported platforms, which are Matisse and Renoir systems, this lists the version of the PM table.

The following table lists the known characteristics per version:

| Hex      | Platform | Table Size (Hex) |
|:--------:|:--------:|:----------------:|
| 0x240902 | Matisse  | 0x514            |
| 0x240903 | Matisse  | 0x518            |
| 0x240802 | Matisse  | 0x7E0            |
| 0x240803 | Matisse  | 0x7E4            |
| 0x370000 | Renoir   | 0x794            |
| 0x370001 | Renoir   | 0x884            |
| 0x370002 | Renoir   | 0x88C            |

Note: File is a 32 bit word encoded in little-endian binary order.

#### `/sys/kernel/ryzen_smu_drv/pm_table`

On supported platforms, this file contains the PM table for the processor, as updated by the SMU.

Note: This file is encoded directly by the SMU and contains an array of 32-bit floating point values
whose structure is determined by the version of the table.

## Module Parameters

The driver supports the following module parameters:

#### `smu_timeout_attempts`

When executing an SMU command, either by reading `pm_table` or manually, via `smu_args` and
`smu_cmd`, the driver will retry this many times before considering the command to have timed out.

For example, on slower or busy systems, the SMU may be tied up resulting in commands taking longer
to execute than normal. Allowed range is from `500` to `32768`, defaulting to `8192`.

#### `smu_pm_update_ms`

When the `pm_table` file is read, the driver first checks for how long since the PM table was last
updated, and if it exceeds `smu_pm_update_ms`, the SMU is first told to update the contents before
the table is shown.

Accepted ranges are in milliseconds, between `50` and `60000`, defaulting to `1000`. It is generally
a good idea to leave this at its default value.

## Example Usage

For Matisse processors, there are several commands that are known to work:

```sh
# Note: Does not persist across reboots

# Set the running PPT to 142 W (argument in milliwatts)
printf '%0*x' 48 142000 | fold -w 2 | tac | tr -d '\n' | xxd -r -p | sudo tee /sys/kernel/ryzen_smu_drv/smu_args && printf '\x53' | sudo tee /sys/kernel/ryzen_smu_drv/rsmu_cmd

# Set the running TDC to 90 A (argument in milliamps)
printf '%0*x' 48 90000 | fold -w 2 | tac | tr -d '\n' | xxd -r -p | sudo tee /sys/kernel/ryzen_smu_drv/smu_args && printf '\x54' | sudo tee /sys/kernel/ryzen_smu_drv/rsmu_cmd

# Set the running EDC to 140 A (argument in milliamps)
printf '%0*x' 48 140000 | fold -w 2 | tac | tr -d '\n' | xxd -r -p | sudo tee /sys/kernel/ryzen_smu_drv/smu_args && printf '\x55' | sudo tee /sys/kernel/ryzen_smu_drv/rsmu_cmd

# Set the PBO Scalar to 2x
# Calculation: 1000 - (scalar * 100)
printf '%0*x' 48 800 | fold -w 2 | tac | tr -d '\n' | xxd -r -p | sudo tee /sys/kernel/ryzen_smu_drv/smu_args && printf '\x58' | sudo tee /sys/kernel/ryzen_smu_drv/rsmu_cmd
```

As to the rest of the commands, we leave that as an exercise up to the user. :)