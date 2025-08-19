# LLM-Evaluator

A comprehensive evaluation framework for Large Language Models (LLMs) focused on code compilation and warning handling capabilities for Linux Device Drivers. This project evaluates LLM performance using static analysis tools and compilation metrics.

## Overview

The LLM-Evaluator assesses language model performance using two primary metrics:
- **Compile Score**: Ratio of successful compilations to total scripts (Error handling is included)
- **Warning Handling Score**: Ratio of warnings resolved to total warnings generated in the initial iteration

The final evaluation score is calculated as: `Total Score = 0.5 × Compile Score + 0.5 × Warning Handling Score`

## Prerequisites

- Oracle VirtualBox
- Ubuntu Server/Desktop ISO



## Installation & Setup

### Step 1: Repository Setup

Clone the LLM-Evaluator repository to your host machine:

```bash
git clone https://github.com/BhanuPratap16/LLM-Evaluator.git
```

### Step 2: Virtual Machine Configuration

1. **Create Ubuntu VM**: Set up a new virtual machine using Oracle VirtualBox with Ubuntu Live Server or Ubuntu Desktop
2. **Install Guest Additions**:
   - Navigate to **Devices** → **Insert Guest Additions CD Image** while the VM is running
3. **Configure Shared Folder**:
   - Go to **Machine** → **Settings** → **Shared Folders**
   - Add the LLM-Evaluator folder path from your host machine
   - Enable **Permanent** and **Auto-mount** options
   - Leave **Read-only** option unchecked

### Step 3: Guest Additions Installation

Install VirtualBox Guest Utilities within the VM:

```bash
sudo apt-get install virtualbox-guest-utils
```

### Step 4: Shared Folder Access Configuration

Update system packages and install required dependencies:

```bash
sudo apt update
sudo apt upgrade
sudo apt install build-essential dkms linux-headers-$(uname -r)
```

Install Guest Additions:

```bash
sudo sh /media/$USER/VBox_GAs_*/VBoxLinuxAdditions.run
```

> **Note**: Replace `VBox_GAs_*` with the actual version folder name. Use `ls /media/$USER` to identify the correct folder name of VBox_GAs_*.

### Step 5: User Permission Configuration

Verify Guest Additions installation:

```bash
getent group vboxsf
```

Expected output: `vboxsf:x:654:$USER`

Add your user to the vboxsf group for shared folder access:

```bash
sudo usermod -aG vboxsf $USER
```

**Important**: Reboot the VM after this step to apply group changes.

### Step 6: Development Tools Installation

Install essential compilation and analysis tools:

```bash
sudo apt-get install cmake make gcc clang clang-tidy llvm lldb bear ninja-build -y
```

### Step 7: Clang-Tidy Configuration

Navigate to the shared folder:

```bash
cd /media/sf_LLM-Evaluator
```

Generate compilation database (compile_commands.json) for clang-tidy:

```bash
bear -- make
```

Remove incompatible compiler flags from the compilation database:

```bash
jq 'map(.arguments |= map(select(. | test("^-fconserve-stack$|^-fno-allow-store-data-races$|^-mindirect-branch-register$|^-mindirect-branch=thunk-extern$|^-mpreferred-stack-boundary=3$|^-fsanitize=bounds-strict$|^-mrecord-mcount$|^-falign-jumps=1$") | not)))' compile_commands.json > compile_commands.tmp && mv compile_commands.tmp compile_commands.json
```

### Step 8: Python Environment Setup

Install Python virtual environment support:

```bash
sudo apt-get install python3-venv -y
```

Create and activate a virtual environment in your home directory only:

```bash
cd ~
python3 -m venv myenv
source myenv/bin/activate
```

### Step 9: Python Dependencies Installation

Navigate to the shared folder and install required packages:

```bash
cd /media/sf_LLM-Evaluator
pip install -r requirements.txt
```

## Usage

Execute the evaluation framework:

```bash
python3 main.py
```

## Evaluation Metrics

### Current Configuration
- **Model**: Gemini-2.5-Flash
- **Primary Metrics**:
  - **Compile Score**: `successful_compilations / total_scripts`
  - **Warning Handling Score**: `warnings_handled / total_warnings_first_iteration`
- **Final Score**: Weighted average of both metrics (50% each)

## Architecture

The evaluation framework consists of:
- Static analysis integration with clang-tidy
- Compilation testing infrastructure
- Warning detection and resolution tracking
- LLM response evaluation pipeline

## Troubleshooting

### Common Issues

1. **Shared folder access denied**: Ensure you've added your user to the vboxsf group and rebooted the VM
2. **Guest Additions not working**: Verify that the Guest Additions ISO is properly mounted and installed
3. **Compilation database errors**: Ensure all development tools are properly installed before running `bear -- make`
4. **Python package installation failures**: Confirm virtual environment is activated and internet connectivity is available

### System Requirements

- **Host System**: Windows/macOS/Linux with VirtualBox support
- **VM Requirements**: 
  - Minimum 4GB RAM
  - 20GB disk space
  - Ubuntu 20.04+ recommended
- **Network**: Internet access for package installations

