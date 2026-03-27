# AurionOS Command Documentation

This document provides a comprehensive list of all functional terminal commands available in AurionOS, including description and example for each to facilitate thorough testing.

<div style="background: #080808; padding: 25px; border-radius: 12px; border: 1px solid #222; color: #eee;">

### System Core
| Command | Example | Description |
|:---|:---|:---|
| **HELP** | `HELP` | Show this list (also `?`) |
| **CLS / CLEAR** | `CLS` | Clear display |
| **VER** | `VER` | Show AurionOS version |
| **TIME / DATE** | `TIME` | Show current RTC time/date |
| **UPTIME** | `UPTIME` | Show session duration |
| **REBOOT** | `REBOOT` | Restart the computer |
| **SHUTDOWN** | `SHUTDOWN` | Power off (ACPI) |
| **EXIT** | `EXIT` | Reboot system |

### File Management
| Command | Example | Description |
|:---|:---|:---|
| **LS / DIR** | `LS /bin` | List directory content |
| **CD** | `CD /users/luka` | Change directory |
| **PWD** | `PWD` | Show current path |
| **MKDIR** | `MKDIR /new_folder` | Create directory |
| **RMDIR** | `RMDIR /old_folder` | Remove directory |
| **TOUCH** | `TOUCH file.txt` | Create empty file |
| **CAT / TYPE** | `CAT readme.md` | Read text file |
| **NANO** | `NANO project.py` | Simple text editor |
| **CP / COPY** | `CP a.txt b.txt` | Copy file |
| **MV / MOVE** | `MV a.txt /tmp/` | Move file |
| **REN / RENAME** | `REN old.txt new.txt` | Rename file |
| **RM / DEL** | `RM temp.log` | Delete file | 
| **FIND** | `FIND "main" app.c` | Search text in file |
| **TREE** | `TREE` | Visual folder tree |
| **STAT** | `STAT kernel.sys` | File metadata |

### Storage & Disk
| Command | Example | Description |
|:---|:---|:---|
| **VOL** | `VOL` | Get volume name |
| **LABEL** | `LABEL SYSTEM` | Set volume label |
| **CHKDSK / FSCK**| `CHKDSK` | Verify FS integrity |
| **FORMAT** | `FORMAT` | Wipe and format disk |
| **DISKPART** | `DISKPART` | Partition info |
| **DF / DU** | `DF -h` | Disk space usage |
| **FREE** | `FREE` | Show free clusters |
| **SYNC** | `SYNC` | Write cache to disk |
| **MOUNT** | `MOUNT /dev/hd0` | Mount block device |
| **LSBLK** | `LSBLK` | List block devices |
| **BLKID** | `BLKID` | Device UUIDs/Labels |
| **READSECTOR** | `READSECTOR 63` | Raw sector access |

### Users & Security
| Command | Example | Description |
|:---|:---|:---|
| **WHOAMI** | `WHOAMI` | Current user name |
| **USERS** | `USERS` | List all users |
| **USERADD** | `USERADD Luka` | Create user profile |
| **USERDEL** | `USERDEL Guest` | Delete user profile |
| **PASSWD** | `PASSWD Luka` | Set user password |
| **LOGIN / LOGOUT**| `LOGIN Luka` | Switch session |
| **SU / SUDO** | `SUDO REBOOT` | Root access |

### System Diagnostics
| Command | Example | Description |
|:---|:---|:---|
| **PS / TASKLIST** | `PS` | Show processes |
| **KILL / TASKKILL**| `KILL 4` | Stop process |
| **TOP** | `TOP` | Resource monitor |
| **MEM** | `MEM` | Physical RAM info |
| **SYSINFO** | `SYSINFO` | Hardware overview |
| **UNAME** | `UNAME -a` | Logic kernel info |
| **HOSTNAME** | `HOSTNAME my-pc` | Get/Set machine name |
| **LSCPU** | `LSCPU` | CPU capabilities |
| **LSPCI** | `LSPCI` | PCI bus devices |
| **DMESG** | `DMESG` | Kernel log buffer |

### Advanced Utilities
| Command | Example | Description |
|:---|:---|:---|
| **CALC** | `CALC 512 / 8` | Calculator |
| **HEXDUMP / OD** | `HEXDUMP app.bin` | Raw hex viewer |
| **HASH** | `HASH secret` | String hashing |
| **WC** | `WC code.c` | Word/Line/Byte count |
| **HEAD / TAIL** | `TAIL boot.log` | File start/end bits |
| **GREP** | `GREP "error" log` | Pattern searching |
| **SORT / UNIQ** | `SORT names.txt` | Data processing |
| **CUT / PASTE** | `CUT -f1 data.csv` | Data extraction |
| **DIFF** | `DIFF v1.c v2.c` | Compare files |
| **BASE64** | `BASE64 -e data` | Encode/Decode |
| **REV** | `REV "Aurion"` | Reverse text |
| **OD** | `OD -x data.bin` | Octal dump |
| **FACTOR** | `FACTOR 128` | Prime factors |
| **SEQ** | `SEQ 1 100` | Generate sequence |
| **TAC** | `TAC log.txt` | CAT in reverse |
| **NL** | `NL code.c` | Number lines |
| **FIGLET / BANNER**| `FIGLET "OS"` | ASCII banners |
| **COWSAY** | `COWSAY "Moo"` | Talking cow |
| **FORTUNE** | `FORTUNE` | Random fortune |
| **STRINGS** | `STRINGS bin.exe` | Pick out text |
| **ASCII** | `ASCII` | Show ASCII chart |

### Developer Tools
| Command | Example | Description |
|:---|:---|:---|
| **PYTHON** | `PYTHON init.py` | Mini-Python 3 |
| **MAKE** | `MAKE` | Run Makefile |
| **GUITEST** | `GUITEST` | GPU diagnostics |
| **NET-TEST** | `NET-TEST` | Network check |

</div>

<br>

<div style="background: #111; padding: 10px; border-left: 4px solid #4ade80; color: #aaa; font-style: italic;">
Tip: Use "SUDO" before any command that requires administrative privileges if you are logged in as a standard user.
</div>
