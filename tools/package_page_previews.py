"""Package render_pages.cpp outputs as labeled contact sheets and a local gallery."""
import argparse
from pathlib import Path
import shutil
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".artifacts/all-pages"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--font", type=Path, default=ROOT / ".artifacts/fonts/NotoSansTC[wght].ttf", help="Noto Sans TC font path")
parser.add_argument("--docs", action="store_true", help="Refresh the curated README/manual demo assets in docs/")
args = parser.parse_args()
FONT = args.font
names = ["今日偷薪", "還能偷多少", "本月戰績", "這份工作撈多少", "現在時刻", "休假倒數", "亮度調整", "系統資訊"]
themes = ["CLASSIC 經典原版", "AMBER 琥珀終端", "HANDHELD 復古掌機"]
font = ImageFont.truetype(str(FONT), 22)
small = ImageFont.truetype(str(FONT), 18)
overview = Image.new("RGB", (1980, 3230), "#e8ecee")
d = ImageDraw.Draw(overview)
for theme, label in enumerate(themes):
    d.text((20 + 660 * theme, 16), label, font=font, fill="#182930")
    sheet = Image.new("RGB", (1300, 1600), "#e8ecee")
    sd = ImageDraw.Draw(sheet)
    sd.text((15, 10), label + " / 全部 8 頁", font=font, fill="#182930")
    for i, name in enumerate(names):
        raw = Image.open(OUT / f"theme_{theme}_page_{i+1}.ppm")
        raw.save(OUT / f"theme_{theme}_page_{i+1}_native.png")
        im = raw.resize((640, 340), Image.Resampling.NEAREST)
        im.save(OUT / f"theme_{theme}_page_{i+1}.png")
        x, y = 10 + (i % 2) * 650, 50 + (i // 2) * 385
        sd.text((x, y), f"{i+1:02d} / {name}", font=small, fill="#182930")
        sheet.paste(im, (x, y + 30))
        ox, oy = 10 + theme * 660, 60 + i * 395
        d.text((ox, oy), f"{i+1:02d} / {name}", font=font, fill="#182930")
        overview.paste(im, (ox, oy + 34))
    sheet.save(OUT / f"all_pages_{theme}.png")
overview.save(OUT / "all_pages_comparison.png")
html = ['<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><title>薪水小偷：全部頁面</title><style>body{background:#e8ecee;color:#182930;font:16px system-ui;margin:24px}nav{display:flex;gap:16px;position:sticky;top:0;background:#e8ecee;padding:12px}section{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:20px}figure{margin:0}img{width:100%;max-width:640px;image-rendering:pixelated}figcaption{margin:10px 0}h2{margin-top:40px}</style><h1>全部 8 頁 × 3 種主題</h1><p>實際韌體渲染。示例：2026/09/23 15:32:08，月薪 NT$40,000，到職日 2026/08/01；系統資訊為示例值。</p><nav>']
html += [f'<a href="#theme-{i}">{label}</a>' for i,label in enumerate(themes)]
html.append('</nav>')
for theme,label in enumerate(themes):
    html.append(f'<h2 id="theme-{theme}">{label}</h2><section>')
    for i,name in enumerate(names):
        path=f'theme_{theme}_page_{i+1}.png'
        html.append(f'<figure><figcaption>{i+1:02d} / {name}</figcaption><a href="{path}"><img src="{path}" alt="{name}"></a></figure>')
    html.append('</section>')
(OUT / 'index.html').write_text('\n'.join(html), encoding='utf-8')
print(OUT / 'index.html')
if args.docs:
    docs = ROOT / "docs"
    host = ROOT / ".artifacts/host"
    manual = docs / "images/manual"
    manual.mkdir(parents=True, exist_ok=True)
    scenes = ["setup", "waiting", "brightness_edit", "brightness_saved", "anniversary",
              "update_prompt", "update_progress", "usb_power"]
    for name in scenes:
        with Image.open(OUT / f"manual_{name}.ppm") as raw:
            raw.resize((640, 340), Image.Resampling.NEAREST).save(manual / f"{name}.png")
    for name, page in (("income", 1), ("rates", 3), ("job_total", 4), ("holiday", 6), ("system", 8)):
        shutil.copyfile(OUT / f"theme_0_page_{page}.png", manual / f"{name}.png")
    theme_strip = Image.new("RGB", (1000, 210), "#e8ecee")
    theme_draw = ImageDraw.Draw(theme_strip)
    for theme, label in enumerate(themes):
        theme_draw.text((10 + theme * 330, 6), label, font=small, fill="#182930")
        with Image.open(OUT / f"theme_{theme}_page_1.ppm") as raw:
            theme_strip.paste(raw, (10 + theme * 330, 34))
    theme_strip.save(manual / "themes.png")
    progress = Image.new("RGB", (1000, 1060), "#e8ecee")
    progress_draw = ImageDraw.Draw(progress)
    for theme, label in enumerate(themes):
        progress_draw.text((10 + theme * 330, 6), label, font=small, fill="#182930")
        for step, percent in enumerate((0, 25, 50, 75, 100)):
            x, y = 10 + theme * 330, 38 + step * 204
            progress_draw.text((x, y), f"{percent}% / {'滿金幣' if percent == 100 else '工時進度'}", font=small, fill="#182930")
            with Image.open(OUT / f"progress_{theme}_{step}.ppm") as raw:
                progress.paste(raw, (x, y + 28))
    progress.save(docs / "coin_progress.png")
    assets = {OUT / "all_pages_0.png": docs / "screens.png",
              OUT / "all_pages_comparison.png": docs / "themes.png",
              host / "boot_0.gif": docs / "boot.gif"}
    for name in ("coin_physics.gif", "stack_settled.png", "lunch.gif", "rest.gif", "holiday.gif"):
        assets[host / name] = docs / name
    missing = [str(source) for source in assets if not source.is_file()]
    if missing:
        raise SystemExit("Run tools/test_host.py and render_pages first. Missing: " + ", ".join(missing))
    for source, target in assets.items():
        shutil.copyfile(source, target)
    print(f"Refreshed {len(assets)} documentation demo assets in {docs}")
    print(f"Refreshed 14 manual illustrations in {manual}")
