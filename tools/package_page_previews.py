"""Package render_pages.cpp outputs as labeled contact sheets and a local gallery."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".artifacts/all-pages"
FONT = ROOT / ".artifacts/fonts/NotoSansTC[wght].ttf"
names = ["今日偷薪", "還能偷多少", "本月戰績", "這份工作撈多少", "現在時刻", "休假倒數", "亮度調整", "系統資訊"]
themes = ["CLASSIC 原始主題", "AMBER 琥珀終端", "HANDHELD 復古掌機"]
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
