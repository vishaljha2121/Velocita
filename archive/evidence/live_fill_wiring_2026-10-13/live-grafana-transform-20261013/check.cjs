const assert=require('node:assert/strict'),fs=require('node:fs');
const root=__dirname+'/node_modules/',p=root+'@grafana/data/dist/cjs/';
assert.equal(require(root+'@grafana/data/package.json').version,'13.1.3');
const {transformDataFrame}=require(p+'transformations/transformDataFrame.cjs');
const {standardTransformersRegistry:registry}=require(p+'transformations/standardTransformersRegistry.cjs');
for (const [file,key] of [['convertFieldType','convertFieldTypeTransformer'],['organize','organizeFieldsTransformer'],['sortBy','sortByTransformer']]) {const t=require(p+'transformations/transformers/'+file+'.cjs')[key];registry.register({id:t.id,name:t.name,transformation:()=>t});}
const {lastValueFrom}=require(root+'rxjs');global.window={};
const dash=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
const chart=dash.panels.find(p=>p.title==='Live SOL entries / exits — effective settled price');
const table=dash.panels.find(p=>p.type==='table');
const field=(name,type,values)=>({name,type,values,config:{}});
const times=[1791929920000,1791929900000];
const frame=(refId,side)=>({refId,length:2,fields:[field('Time','time',[1791930000000,1791930000000]),field('Value','number',[112,111]),field('first_observed_ms','string',times.map(String)),field('side','string',[side,side]),field('effect','string',['2','1']),field('quantity_sol','string',['0.008','0.009']),field('quote_usdc','string',['0.896','0.999']),field('fee_usdc','string',['0.002','0.002'])]});
(async()=>{
  for (const input of [[frame('A','buy'),frame('B','sell')],[frame('A','buy')],[frame('B','sell')],[]]) {
    // Grafana Prometheus 13.1.3 names multi-query values Value #A/#B.
    if(input.length>1)for(const f of input)f.fields.find(x=>x.name==='Value').name='Value #'+f.refId;
    const result=await lastValueFrom(transformDataFrame(chart.transformations,input));
    assert.equal(result.length,input.length);
    for (const f of result) {
      assert.equal(f.fields.length,2);
      const t=f.fields.find(x=>x.type==='time'),v=f.fields.find(x=>x.type==='number');
      assert(t);assert(v);assert.equal(t.name,'first_observed_ms');
      assert.equal(t.config.displayName,'First confirmed observation');
      assert.equal(v.config.displayName,f.refId==='A'?'Entry / buy':'Exit / sell');
      assert.deepEqual(t.values,[...times].sort((a,b)=>a-b));
      assert.deepEqual(v.values,[111,112]);
      assert.equal(f.length,2);
    }
  }
  const result=await lastValueFrom(transformDataFrame(table.transformations,[frame('A','buy')]));
  const f=result[0];assert.equal(f.fields.length,7);
  assert.equal(f.fields.find(x=>x.name==='first_observed_ms').type,'time');
  assert.deepEqual(f.fields.find(x=>x.name==='first_observed_ms').values,times);
  assert.deepEqual(f.fields.find(x=>x.name==='quantity_sol').values,[.008,.009]);
  assert.deepEqual(f.fields.find(x=>x.name==='fee_usdc').values,[.002,.002]);
  console.log(JSON.stringify({grafana_data:'13.1.3',chart_cases:4,table_cases:1,passed:true,browser_rendered:false}));
})().catch(e=>{console.error(e.stack);process.exit(1)});
