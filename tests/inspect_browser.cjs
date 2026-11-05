// Headless regression test for our local diagnostic viewer. No external requests.
const {chromium}=require('playwright');
const fs=require('fs'),path=require('path'),{pathToFileURL}=require('url');
(async()=>{
 const report=path.resolve(process.argv[2]),output=path.resolve(process.argv[3]);fs.mkdirSync(output,{recursive:true});
 const browser=await chromium.launch({executablePath:process.env.FP_BROWSER||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
 try{
  const page=await browser.newPage({viewport:{width:1450,height:1100},deviceScaleFactor:1});const errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(pathToFileURL(report).href);await page.waitForFunction(()=>document.querySelector('canvas').dataset.ready==='true');
  const data=await page.locator('#data').textContent().then(JSON.parse);
  if(!data.pairs.length)throw Error('No diagnostics');
  const select=async p=>{await page.locator('.pair[data-pair-id="'+p.id+'"]').click();await page.waitForFunction(id=>{const c=document.querySelector('canvas');return c.dataset.pair===id&&c.dataset.ready==='true';},p.id);};
  const failure=data.pairs.find(p=>p.status!=='OK');if(failure){await select(failure);if(!await page.locator('#view option[value=aligned]').evaluate(e=>e.disabled))throw Error('Failed extraction showed fabricated alignment');await page.screenshot({path:path.join(output,'failure.png')});}
  const genuine=data.pairs.find(p=>p.genuine&&p.status==='OK'&&p.hasAlignment);if(!genuine)throw Error('No valid genuine inspection');await select(genuine);
  await page.screenshot({path:path.join(output,'genuine-split.png')});
  await page.locator('#view').selectOption('aligned');await page.waitForFunction(()=>document.querySelector('canvas').dataset.view==='aligned'&&document.querySelector('canvas').dataset.ready==='true');
  await page.locator('#matches tr').first().click();await page.waitForFunction(()=>document.querySelector('canvas').dataset.ready==='true');
  await page.screenshot({path:path.join(output,'genuine-aligned.png')});
  const before=await page.locator('#stats').textContent();await page.locator('#quality').evaluate(e=>{e.value='80';e.dispatchEvent(new Event('input',{bubbles:true}));});await page.waitForFunction(()=>document.querySelector('canvas').dataset.ready==='true');
  if(before!==await page.locator('#stats').textContent())throw Error('Display filter changed score/statistics');
  await page.locator('#quality').evaluate(e=>{e.value='0';e.dispatchEvent(new Event('input',{bubbles:true}));});await page.waitForFunction(()=>document.querySelector('canvas').dataset.ready==='true');
  const downloadPromise=page.waitForEvent('download');await page.locator('#export').click();const download=await downloadPromise;await download.saveAs(path.join(output,'export.png'));
  if(fs.readFileSync(path.join(output,'export.png')).subarray(1,4).toString()!=='PNG')throw Error('PNG export failed');
  await page.locator('#filter').selectOption('impostor');const impostor=data.pairs.find(p=>!p.genuine&&p.status==='OK'&&p.hasAlignment);await select(impostor);
  await page.locator('#view').selectOption('split');await page.waitForFunction(()=>document.querySelector('canvas').dataset.view==='split'&&document.querySelector('canvas').dataset.ready==='true');
  await page.screenshot({path:path.join(output,'impostor.png')});
  await page.locator('#search').fill('no-such-image-xyz');if(await page.locator('.pair').count()!==0)throw Error('Search did not filter');
  if(errors.length)throw Error(errors.join('\n'));
  console.log('PASS: local viewer, failure state, genuine/impostor navigation, alignment, correspondence selection, display-only quality filter, search and PNG export.');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exit(1);});
