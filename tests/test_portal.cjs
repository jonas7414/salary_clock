// npm install --no-save playwright; node tests/test_portal.cjs
// CHROME_PATH can point at an already installed Chrome/Chromium executable.
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
(async()=>{
 const browser=await chromium.launch(process.env.CHROME_PATH?{executablePath:process.env.CHROME_PATH}:{channel:'chrome'});
 try {
  for(const width of [320,390,1024]) {
   const page=await browser.newPage({viewport:{width,height:844}}),errors=[];
   page.on('pageerror',e=>errors.push(e.message));
   let posted=null,reboots=0;
   const config={config_version:1,wifi_ssid:'Test AP',has_password:true,monthly_salary:40000,work_days:31,timezone:'Asia/Taipei',display_theme:0,work_start:'09:00',lunch_start:'12:00',lunch_end:'13:00',work_end:'18:00',display_on:'08:00',display_off:'19:00',page_order:'01264573',anniversary_name:'',anniversary_date:'',anniversary_annual:1};
   if(width===1024)config.ntp_server=1;
   await page.route('http://salary-clock.test/**',async route=>{
    const req=route.request(),url=new URL(req.url());
    let body={};
    if(url.pathname==='/')return route.fulfill({contentType:'text/html; charset=utf-8',body:fs.readFileSync(path.join(__dirname,'../components/setup_portal/web/index.html'),'utf8')});
    if(url.pathname==='/api/config' && req.method()==='GET')body=config;
    else if(url.pathname==='/api/config'){posted=req.postDataJSON();body={saved:true};assert.equal(req.headers()['x-salarythief-request'],'setup');}
    else if(url.pathname==='/api/reboot'){reboots++;body={accepted:true};}
    await route.fulfill({contentType:'application/json',body:JSON.stringify(body)});
   });
   await page.goto('http://salary-clock.test/');await page.waitForFunction(()=>!document.getElementById('save').disabled);
   assert.equal(await page.getByLabel('NTP 伺服器',{exact:true}).inputValue(),width===1024?'1':'0');
   await page.getByLabel('NTP 伺服器',{exact:true}).selectOption('1');
   await page.getByRole('link',{name:'薪資與工時',exact:true}).click();
   assert.equal(await page.evaluate(()=>document.querySelector('#salary-settings').getBoundingClientRect().top>=70),true);
   await page.getByRole('radio',{name:'琥珀終端',exact:true}).check();
   const names=()=>page.locator('.page-name').allTextContents();
   assert.equal((await names()).at(-1),'8. 系統資訊');
   await page.getByRole('button',{name:'系統資訊上移'}).click();
   assert.equal((await names())[6],'7. 系統資訊');
   if(width>500){await page.locator('#page-settings').evaluate(el=>el.scrollIntoView({block:'start',behavior:'instant'}));await page.locator('.page-item').last().dragTo(page.locator('.page-item').first());assert.equal((await names())[0],'1. 亮度調整');}
   await page.getByRole('button',{name:'恢復預設順序'}).click();assert.equal((await names()).at(-1),'8. 系統資訊');
   assert.equal(await page.getByLabel('到職日',{exact:true}).inputValue(),'2026-08-01');
   await page.getByLabel('到職日',{exact:true}).fill('2025-08-01');
   await page.getByLabel('紀念日名稱',{exact:true}).fill('我們的紀念日');
   await page.getByLabel('紀念日期',{exact:true}).fill('2024-02-29');
   await page.getByLabel('每年重複紀念',{exact:true}).uncheck();
   await page.getByRole('button',{name:'新增紀念日',exact:true}).click();
   await page.getByLabel('紀念日名稱 2',{exact:true}).fill('生日');
   await page.getByLabel('紀念日期 2',{exact:true}).fill('2020-08-01');
   for(let i=0;i<3;i++)await page.getByRole('button',{name:'新增紀念日',exact:true}).click();
   assert.equal(await page.getByRole('button',{name:'新增紀念日',exact:true}).isDisabled(),true);
   await page.getByLabel('紀念日名稱 5',{exact:true}).fill('Delete me');
   await page.getByLabel('紀念日期 5',{exact:true}).fill('2026-09-01');
   await page.getByRole('button',{name:'刪除紀念日 5',exact:true}).click();
   assert.equal(await page.getByRole('button',{name:'新增紀念日',exact:true}).isEnabled(),true);
   await page.getByRole('button',{name:'現在時刻上移'}).click();
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
   fs.mkdirSync(path.join(__dirname,'../.artifacts/host'),{recursive:true});
   await page.evaluate(()=>window.scrollTo({top:0,behavior:'instant'}));
   await page.screenshot({path:path.join(__dirname,`../.artifacts/host/portal-${width}-viewport.png`)});
   await page.screenshot({path:path.join(__dirname,`../.artifacts/host/portal-${width}.png`),fullPage:true,style:'nav,.save-bar{position:static!important}'});
   await page.getByLabel('紀念日名稱',{exact:true}).fill('Emoji 😀');
   await page.getByRole('button',{name:'儲存並重新開機'}).click();assert.equal(posted,null);
   assert.match(await page.locator('#message').innerText(),/不支援 emoji/);
   await page.getByLabel('紀念日名稱',{exact:true}).fill('我們的紀念日');
   await page.getByRole('button',{name:'儲存並重新開機'}).click();await page.waitForFunction(()=>document.getElementById('message').textContent.includes('設定已儲存'));
   assert.equal(posted.page_order,'01246573');assert.equal(posted.anniversary_name,'我們的紀念日');
   assert.equal(posted.job_start_date,'2025-08-01');
   assert.equal(posted.ntp_server,1);
   assert.equal(posted.display_theme,1);assert.equal(posted.monthly_salary,40000);
   assert.equal(posted.anniversary_date,'2024-02-29');assert.equal(posted.anniversary_annual,0);
   assert.equal(posted.anniversary_name_2,'生日');assert.equal(posted.anniversary_date_2,'2020-08-01');assert.equal(posted.anniversary_annual_2,1);
   assert.equal(posted.anniversary_name_5,'');assert.equal(posted.anniversary_date_5,'');
   assert.equal('wifi_password' in posted,false);assert.equal(reboots,1);assert.deepEqual(errors,[]);
   Object.assign(config,posted);await page.reload();await page.waitForFunction(()=>!document.getElementById('save').disabled);
   assert.equal(await page.getByLabel('紀念日名稱 2',{exact:true}).inputValue(),'生日');
   assert.equal(await page.getByLabel('紀念日期 2',{exact:true}).inputValue(),'2020-08-01');
   assert.equal(await page.getByLabel('每年重複紀念 2',{exact:true}).isChecked(),true);
   assert.equal(await page.getByLabel('紀念日名稱 5',{exact:true}).isVisible(),false);
   await page.close();
  }
  console.log('PASS: mobile/desktop portal ordering, drag, reset, anniversary validation, save, password preservation and layout');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
