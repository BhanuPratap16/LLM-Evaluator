# LLM-Evaluator

First, Download this repos through git clone https://github.com/BhanuPratap16/LLM-Evaluator.git

Then, Create a VM machine(ubuntu live server or ubuntu desktop) through Oracle Virtualbox 
Now, we need to setup connection between VM machine and Host pc for running linux files follow through


Go to Devices and then select "Insert Guest Addition iso image"
then, Go and select Machines while VM is running , add the shared folder (here, add LLM-Evaluator folder path) and tick Permanent and auto-mount options and leave read-only option

Now, within VM use the command to install

bash ```
sudo apt-get install virtualbox-guest-utils
```
Now, You can find your shared folder within /media/sf_Shared 

Now, It might happen that you have no access to enter into this directory.
So, to gain access, execute these commands 

bash ```
sudo apt update
sudo apt upgrade
sudo apt install build-essential dkms linux-headers-$(uname -r)

sudo sh /media/$USER/VBox_GAs_*/VBoxLinuxAdditions.run   

```
VBox_GAs_* (here * is the version of your VBox_GAs) Use ls /media/$USER to peek into directory to see full name of the folder
Here, $USER is ubuntu username(or VM username)

Confirm that it got installed
run the following command

bash ```
getent group vboxsf
```
If you see something like vboxsf::x:654::$USER
then its installed

Now, Run the below command to add your user in order to access shared folder(LLM-Evaluator)
bash ``` 
sudo usermod -aG vboxsf $USER
```

Important modules to install 
sudo apt-get install cmake make gcc clang clang-tidy llvm lldb bear ninja-build -y

<!-- Makefile content - dont include ldd.c  as dependencies in add :  -->


Now, In order to run clang-tidy without any error we need to generate compile_commands.json file to get reference of supporting modules to compile
Run this command within the shared folder
bash```
bear -- make  
```


and After that, we need to remove non important flags which clang-tidy has nothing to do with, you need to run the below code to remove those flags through this command
bash```
jq 'map(.arguments |= map(select(. | test("^-fconserve-stack$|^-fno-allow-store-data-races$|^-mindirect-branch-register$|^-mindirect-branch=thunk-extern$|^-mpreferred-stack-boundary=3$|^-fsanitize=bounds-strict$|^-mrecord-mcount$|^-falign-jumps=1$") | not)))' compile_commands.json > compile_commands.tmp && mv compile_commands.tmp compile_commands.json
```

We have finished the setup of clang-tidy


Now, IN order to run the python main.py file 
Follow the along 

sudo apt-get install python3-venv -y 

create and activate virtual environment in your home directory and not in shared folder as it is not allowed to do 

python3 -m venv myenv  

source myenv/bin/activate

Then, after activation of virtual environment , head to shared folder cd /media/sf_LLM_Evaluator and run the below command
pip install -r requirements.txt

Now, Finally Run python3 main.py file 


Evaluation Metric
Here, I have used gemini-2.5-flash model 
For rubric,I have choosen compile score which is number of successful_compilation/total scripts and warninghandling score which is (number of warnings handled)/(total warnings generated in first iteration)

Total Score - 0.5 * compile score + 0.5 * warninghandling score
