# LLM-Evaluator

To connect Shared Cscripts with VM

Go to Devices and then select "Insert Guest Addition iso image"
then, Go and select Machines while VM is running , add the shared folder(name it as "Shared") and give tick Permanent and auto-mount options and leave read-only

Now, within VM use the command to install

bash ```
sudo apt-get install virtualbox-guest-utils
```
Now, You can find your shared folder within /media/sf_Shared 

Now, It might happen that you have no access to enter into this directory.
So, to gain access, execute these commands 

bash ```
sudo apt update
sudo apt install build-essential dkms linux-headers-$(uname -r)

sudo sh /media/$USER/VBox_GAs_*/VBoxLinuxAdditions.run   

```
Here, $USER is ubuntu username(or VM username)

Once confirm that you are added 
run the following command

bash ```
getent group vboxsf
```
If you see something like vboxsf::x:654::$USER
then its installed

Now, Run the below command to add your user 
bash ``` 
sudo usermod -aG vboxsf $USER
```

Important modules
sudo apt-get install cmake make gcc clang clang-tidy llvm lldb bear ninja-build -y

Makefile content dont include ldd.c  as dependencies in add : 


Now, In order to run clang-tidy without any error we need to generate compile_commands.json file to get reference of supporting modules to compile
bash```
bear -- make  
```
command to do so. 

and After that, we need to remove non important flags which clang-tidy has nothing to do so, you need to run the below code to remove those flags through this command
bash```
jq 'map(.arguments |= map(select(. | test("^-fconserve-stack$|^-fno-allow-store-data-races$|^-mindirect-branch-register$|^-mindirect-branch=thunk-extern$|^-mpreferred-stack-boundary=3$|^-fsanitize=bounds-strict$|^-mrecord-mcount$|^-falign-jumps=1$") | not)))' compile_commands.json > compile_commands.tmp && mv compile_commands.tmp compile_commands.json
```
Now, Run the following command to run clang-tidy 
bash```
clang-tidy ldd.c -p . --extra-arg=-I/lib/modules/$(uname -r)/build/include -export-fixes=tidy_fixes.yaml &> clang_tidy_output.txt
```





