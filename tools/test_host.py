"""Compile and execute the real portable C++ code, plus produce LCD previews.

python tools/test_host.py --cxx clang++
python tools/test_host.py --zig .tools/venv/Lib/site-packages/ziglang/zig.exe
"""
import argparse
import calendar
import json
import os
from pathlib import Path
import subprocess
import sys
from PIL import Image, ImageDraw

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/".artifacts"/"host"

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--zig")
    parser.add_argument("--cjson-dir",type=Path,help="ESP-IDF components/json/cJSON directory")
    args=parser.parse_args()
    subprocess.run([sys.executable,str(ROOT/"tools/update_taiwan_calendar.py"),"--check"],check=True)
    subprocess.run([sys.executable,"-m","unittest","discover","-s","tests","-p","test_taiwan_calendar.py"],cwd=ROOT,check=True)
    OUT.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy()
    env["ZIG_GLOBAL_CACHE_DIR"]=str(ROOT/".tools"/"zig-cache")
    env["ZIG_LOCAL_CACHE_DIR"]=str(ROOT/".tools"/"zig-local")
    compiler=[args.zig,"c++"] if args.zig else [args.cxx]
    calendar_binary=OUT/("test_calendar_download.exe" if os.name=="nt" else "test_calendar_download")
    subprocess.run(compiler+["-std=c++17","-O2","-Wall","-Wextra","-Werror","-pthread",
        "-Icomponents/app_core/include","-Icomponents/calendar_manager/include","tests/test_calendar_download.cpp",
        "components/app_core/taiwan_calendar.cpp","components/calendar_manager/calendar_json.cpp",
        "components/calendar_manager/calendar_cache.cpp","-o",str(calendar_binary)],cwd=ROOT,env=env,check=True)
    subprocess.run([str(calendar_binary)],cwd=ROOT,env=env,check=True)
    button_binary=OUT/("test_button.exe" if os.name=="nt" else "test_button")
    subprocess.run(compiler+["-std=c++17","-O2","-Wall","-Wextra","-Werror",
        "-Icomponents/app_core/include","tests/test_button.cpp","components/app_core/button_logic.cpp",
        "-o",str(button_binary)],cwd=ROOT,env=env,check=True)
    subprocess.run([str(button_binary)],cwd=ROOT,env=env,check=True)
    pages_binary=OUT/("test_extra_pages.exe" if os.name=="nt" else "test_extra_pages")
    subprocess.run(compiler+["-std=c++17","-O2","-Wall","-Wextra","-Werror","-pthread",
        "-Icomponents/app_core/include","-Icomponents/display/include","-Icomponents/coin_physics/include",
        "tests/test_extra_pages.cpp","components/app_core/holiday_countdown.cpp","components/app_core/taiwan_calendar.cpp",
        "components/app_core/button_logic.cpp","components/display/ui_renderer.cpp","-o",str(pages_binary)],cwd=ROOT,env=env,check=True)
    subprocess.run([str(pages_binary),str(OUT)],cwd=ROOT,env=env,check=True)
    sources=["tests/test_core.cpp","components/app_core/app_types.cpp","components/app_core/salary_math.cpp",
             "components/app_core/taiwan_calendar.cpp","components/app_core/battery_status.cpp",
             "components/app_core/button_logic.cpp","components/coin_physics/coin_physics.cpp",
             "components/display/ui_animation.cpp","components/display/ui_renderer.cpp"]
    binary=OUT/("test_core.exe" if os.name=="nt" else "test_core")
    command=compiler+["-std=c++17","-O2","-Wall","-Wextra","-Werror"]
    version=(ROOT/"version.txt").read_text(encoding="utf-8").strip()
    command += [f'-DAPP_FIRMWARE_VERSION="{version}"']
    command += ["-Icomponents/"+x+"/include" for x in ["app_core","coin_physics","display"]]
    command += ["-Icomponents/display"]+sources+["-o",str(binary)]
    subprocess.run(command,cwd=ROOT,env=env,check=True)
    oracle=OUT/"calendar.txt"
    with oracle.open("w",encoding="ascii") as f:
        for entry in json.loads((ROOT/"data/taiwan_calendar.json").read_text(encoding="utf-8"))["years"]:
            y=entry["year"]
            for m,days in enumerate(entry["workdays"],1):
                assert len(days)==calendar.monthrange(y,m)[1]
                index=0
                for d,working in enumerate(days,1):
                    index+=int(working)
                    f.write(f"{y} {m} {d} {working} {days.count('1')} {index}\n")
    result=subprocess.run([str(binary),str(oracle),str(OUT)],cwd=ROOT,env=env,capture_output=True,text=True)
    print(result.stdout,end="")
    if result.returncode:
        print(result.stderr,end="")
        result.check_returncode()
    (OUT/"results.txt").write_text(result.stdout,encoding="utf-8")
    rtc_binary=OUT/("test_rtc.exe" if os.name=="nt" else "test_rtc")
    subprocess.run(compiler+["-std=c++17","-O2","-Wall","-Wextra","-Werror",
        "-Itests/stubs","-Icomponents/time_manager/include","tests/test_rtc.cpp",
        "components/time_manager/ds3231_time.cpp","components/time_manager/ds3231.cpp",
        "-o",str(rtc_binary)],cwd=ROOT,env=env,check=True)
    rtc_result=subprocess.run([str(rtc_binary)],cwd=ROOT,env=env,check=True,capture_output=True,text=True)
    print(rtc_result.stdout,end="")
    with (OUT/"results.txt").open("a",encoding="utf-8") as f:f.write(rtc_result.stdout)
    stack_binary=OUT/("test_coin_stack.exe" if os.name=="nt" else "test_coin_stack")
    subprocess.run(compiler+["-std=c++17","-O2","-Wall","-Wextra","-Werror",
        "-Icomponents/coin_physics/include","tests/test_coin_stack.cpp",
        "components/coin_physics/coin_physics.cpp","-o",str(stack_binary)],cwd=ROOT,env=env,check=True)
    stack_result=subprocess.run([str(stack_binary)],cwd=ROOT,env=env,check=True,capture_output=True,text=True)
    print(stack_result.stdout,end="")
    with (OUT/"results.txt").open("a",encoding="utf-8") as f:f.write(stack_result.stdout)
    nvs_binary=OUT/("test_nvs.exe" if os.name=="nt" else "test_nvs")
    subprocess.run(compiler+["-std=c++17","-O2","-Wall","-Wextra","-Werror","-Wno-unused-const-variable",
        "-Itests/stubs","-Icomponents/app_config/include","-Icomponents/app_core/include",
        "tests/test_nvs.cpp","components/app_config/app_config.cpp","components/app_core/app_types.cpp",
        "-o",str(nvs_binary)],cwd=ROOT,env=env,check=True)
    nvs_result=subprocess.run([str(nvs_binary)],cwd=ROOT,env=env,check=True,capture_output=True,text=True)
    print(nvs_result.stdout,end="")
    with (OUT/"results.txt").open("a",encoding="utf-8") as f:f.write(nvs_result.stdout)
    cjson=args.cjson_dir
    if cjson is None:
        roots=[Path(env.get("PLATFORMIO_CORE_DIR",ROOT/".tools"/"platformio")),Path.home()/".platformio"]
        cjson=next((p/"packages/framework-espidf/components/json/cJSON" for p in roots
                    if (p/"packages/framework-espidf/components/json/cJSON/cJSON.c").exists()),None)
    if cjson is None or not (cjson/"cJSON.c").exists():
        raise SystemExit("JSON tests require ESP-IDF's cJSON. Run pio run first or specify --cjson-dir.")
    cjson_object=OUT/"cJSON.o"
    cc=[args.zig,"cc"] if args.zig else [args.cxx,"-x","c"]
    subprocess.run(cc+["-std=c99","-O2","-c",str(cjson/"cJSON.c"),"-o",str(cjson_object)],cwd=ROOT,env=env,check=True)
    json_binary=OUT/("test_json.exe" if os.name=="nt" else "test_json")
    subprocess.run(compiler+["-std=c++17","-O2","-Wall","-Wextra","-Werror",
        "-Icomponents/app_config/include","-Icomponents/app_core/include","-I"+str(cjson),
        "tests/test_json.cpp","components/app_config/config_json.cpp","components/app_core/app_types.cpp",
        str(cjson_object),"-o",str(json_binary)],cwd=ROOT,env=env,check=True)
    json_result=subprocess.run([str(json_binary)],cwd=ROOT,env=env,check=True,capture_output=True,text=True)
    print(json_result.stdout,end="")
    with (OUT/"results.txt").open("a",encoding="utf-8") as f:f.write(json_result.stdout)
    ota_binary=OUT/("test_ota.exe" if os.name=="nt" else "test_ota")
    subprocess.run(compiler+["-std=c++17","-O2","-Wall","-Wextra","-Werror",
        "-Icomponents/ota_manager/include","-Icomponents/app_core/include","-I"+str(cjson),
        "tests/test_ota.cpp","components/ota_manager/ota_policy.cpp",str(cjson_object),
        "-o",str(ota_binary)],cwd=ROOT,env=env,check=True)
    ota_result=subprocess.run([str(ota_binary)],cwd=ROOT,env=env,check=True,capture_output=True,text=True)
    print(ota_result.stdout,end="")
    with (OUT/"results.txt").open("a",encoding="utf-8") as f:f.write(ota_result.stdout)
    sheet=Image.new("RGB",(680,1200),"#e8ecee")
    draw=ImageDraw.Draw(sheet)
    labels=["01 / Main","02 / Remaining","03 / Month","04 / System","Setup","Waiting for SNTP","Before work","Lunch","After work","Day off","Hold button"]
    for i,label in enumerate(labels):
        x=10+(i%2)*340;y=10+(i//2)*198
        draw.text((x,y),label,fill="#182930")
        img=Image.open(OUT/f"scene_{i}.ppm");sheet.paste(img,(x,y+18))
        img.save(OUT/f"scene_{i}.png")
    sheet.save(OUT/"screens.png")
    themes=Image.new("RGB",(1000,1110),"#e8ecee")
    theme_draw=ImageDraw.Draw(themes)
    for theme,name in enumerate(("CLASSIC / Original", "AMBER / Terminal", "HANDHELD / Retro")):
        x=10+theme*330
        theme_draw.text((x,10),name,fill="#182930")
        for row,scene in enumerate((0,7,8,1,2,3)):
            themes.paste(Image.open(OUT/f"theme_{theme}_scene_{scene}.ppm"),(x,30+row*180))
    themes.save(OUT/"themes.png")
    Image.open(OUT/"stack_settled.ppm").resize((960,510),Image.Resampling.NEAREST).save(OUT/"stack_settled.png")
    frames=[Image.open(OUT/f"coin_{i}.ppm").resize((640,340),Image.Resampling.NEAREST) for i in range(400)]
    frames[0].save(OUT/"coin_physics.gif",save_all=True,append_images=frames[1:],duration=40,loop=0)
    for scene,count in (("lunch",120),("rest",100),("holiday",100)):
        frames=[Image.open(OUT/f"{scene}_{i}.ppm").resize((640,340),Image.Resampling.NEAREST) for i in range(count)]
        frames[0].save(OUT/f"{scene}.gif",save_all=True,append_images=frames[1:],duration=40,loop=0)
    gain_frames=[]
    for i in range(25):
        frame=Image.new("RGB",(980,170),"#e8ecee")
        for theme in range(3):
            frame.paste(Image.open(OUT/f"gain_{theme}_{i}.ppm"),(theme*330,0))
        gain_frames.append(frame.resize((1470,255),Image.Resampling.NEAREST))
    gain_frames[0].save(OUT/"money_gain.gif",save_all=True,append_images=gain_frames[1:],duration=40,loop=0)
    transitions=Image.new("RGB",(1000,935),"#e8ecee")
    for event,name in enumerate(("work_start", "lunch_start", "work_resume", "work_end", "holiday_start")):
        frames=[Image.open(OUT/f"transition_{event}_0_{i}.ppm").resize((640,340),Image.Resampling.NEAREST) for i in range(85)]
        frames[0].save(OUT/f"{name}.gif",save_all=True,append_images=frames[1:],duration=40,loop=0)
        for theme in range(3):
            transitions.paste(Image.open(OUT/f"transition_{event}_{theme}_30.ppm"),(10+theme*330,10+event*185))
    transitions.save(OUT/"transitions.png")
    rtc_sheet=Image.new("RGB",(1000,750),"#e8ecee")
    for theme in range(3):
        rtc_sheet.paste(Image.open(OUT/f"rtc_mode_{theme}.ppm"),(10+theme*330,10))
        for action in range(3):
            rtc_sheet.paste(Image.open(OUT/f"rtc_{action}_{theme}_30.ppm"),(10+theme*330,195+action*185))
    rtc_sheet.save(OUT/"rtc.png")
    battery_sheet=Image.new("RGB",(1000,930),"#e8ecee")
    for theme in range(3):
        for state in range(5):
            battery_sheet.paste(Image.open(OUT/f"battery_{theme}_{state}.ppm"),(10+theme*330,10+state*185))
    battery_sheet.save(OUT/"battery.png")
    boot_sheet=Image.new("RGB",(1000,950),"#e8ecee")
    for theme in range(3):
        frames=[Image.open(OUT/f"boot_{theme}_{i}.ppm").resize((640,340),Image.Resampling.NEAREST) for i in range(126)]
        frames[0].save(OUT/f"boot_{theme}.gif",save_all=True,append_images=frames[1:],duration=[40]*125+[1000],loop=0)
        for row,index in enumerate((0,10,30,80,125)):
            boot_sheet.paste(Image.open(OUT/f"boot_{theme}_{index}.ppm"),(10+theme*330,10+row*185))
    boot_sheet.save(OUT/"boot.png")
    boot_routes=Image.new("RGB",(1000,935),"#e8ecee")
    for theme in range(3):
        for state in range(5):
            boot_routes.paste(Image.open(OUT/f"boot_route_{theme}_{state}.ppm"),(10+theme*330,10+state*185))
    boot_routes.save(OUT/"boot_routes.png")
    for action,name in enumerate(("rtc_read","rtc_sync","rtc_failed")):
        frames=[Image.open(OUT/f"rtc_{action}_0_{i}.ppm").resize((640,340),Image.Resampling.NEAREST) for i in range(81)]
        frames[0].save(OUT/f"{name}.gif",save_all=True,append_images=frames[1:],duration=40,loop=0)
    print(f"Previews: {OUT / 'screens.png'}; {OUT / 'coin_physics.gif'}; {OUT / 'lunch.gif'}; {OUT / 'rest.gif'}; {OUT / 'money_gain.gif'}; {OUT / 'transitions.png'}")

if __name__=="__main__":main()
