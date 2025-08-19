import requests
import json
from google import genai
import os
import subprocess,re,yaml
from tqdm import tqdm


from dotenv import load_dotenv ,find_dotenv
load_dotenv(find_dotenv())

iterations=5
errors=[]
warnings=[]

api_key=os.getenv("google_ai_api_key")

with open("config.json",'r') as f:
    data=json.load(f)
    
# print(api_key)
    
    

questions=data['questions']
style=data['coding-style']
model=data['model']
client=genai.Client(api_key=api_key)

total_warning=0


for i in tqdm(range(iterations), desc="Running Iterations and Scoring"):
    current_warnings=0
    for j in tqdm(range(len(questions)),desc="Generating Code"):
        if i==0:
            response=client.models.generate_content(
                model=model,contents=f"{questions[0]} - only provide code and refer to {style} for proper coding style and nothing else also make sure to remove 'c ``` at starting and ``` in' the end of the code and keep author name as Bhanu"
            )
            rtext=response.text
            first_line = rtext.splitlines()[0]
            
            if first_line.strip() == "```c":
                rtext.pop(0)
                rtext.pop(len(rtext)-1)
            else:
                # print("First line is something else")
                print("NO ``c \n")

            with open(f"temp_ldd/ldd_{j}.c",'w') as f:
                f.write(response.text)
            with open("ldd.c",'w') as f:
                f.write(response.text)
        else:
            with open(f"fixes/tidy_fixes_{j}.yaml",'r') as f:
                fixes=yaml.safe_load(f)
                
            with open(f"temp_ldd/ldd_{j}.c",'r') as f:
                fix_code=f.read()
                
            response=client.models.generate_content(
                model=model,contents=f"Given <br> {fix_code} <br> ,these are the errors in it:{fixes}, fix the code and only provide code and nothing else also keep in mind to remove c ``` at starting and ``` in the end of the code and keep author name as Bhanu"
            )
            rtext=response.text
            first_line = rtext.splitlines()[0]
            
            if first_line.strip() == "```c":
                rtext.pop(0)
                rtext.pop(len(rtext)-1)
            else:
                # print("First line is something else")
                print("NO ``c \n")

            with open(f"temp_ldd/ldd_{j}.c",'w') as f:
                f.write(response.text)
            
            with open(f"ldd.c","w") as f:
                f.write(response.text)
                
        
        cmd = ["clang-tidy","ldd.c","-p",".","--extra-arg=-I/lib/modules/$(uname -r)/build/include",f"-export-fixes=fixes/tidy_fixes_{j}.yaml"]
        out = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
        text = out.stdout
        
        warning = len(re.findall(r":\d+:\d+:\s+warning:", text))
        error   = len(re.findall(r":\d+:\d+:\s+error:", text))
        if i==0:
            warnings.append(warning)
            errors.append(error)            
        else:
            warnings[j]=warning
            errors[j]=error
            
        # except Exception as e:
        #     print(f"Error occured : \n {e}")
            
    compile_rate=0
    warninghandling_score=0
    for j in errors:
        if j==0:
            compile_rate+=1
    if i==0:
        for j in warnings:
            total_warning+=j
        current_warnings=total_warning
    else:
        
        for j in warnings:
            current_warnings+=j
        
    warninghandling_score=(total_warning-current_warnings)/total_warning
    
    compile_score=compile_rate/5
    total_score=warninghandling_score*0.5 + compile_score*0.5
    
    entry={
        "iteration": i+1,
        "Unsuccessful compilation":5-compile_rate,
        "warnings":current_warnings,
        "compile_score": compile_score,
        "warninghandling_score": warninghandling_score,
        "total_score": total_score
    }
    filename="scores.yaml"
    if os.path.exists(filename):
        with open(filename,'r') as f:
            data=yaml.safe_load(f) or []
    else:
        data=[]
        
    data.append(entry)
    
    with open(filename,'w') as f:
        yaml.dump(data,f,default_flow_style=False)
        
        
            
    
    
        
        
        
        