"""Refresh the same six public route accounts; no new quote, signing or execution."""
import importlib.util
import json
from pathlib import Path

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('previous_reader',HERE.parent/'fresh-adoption-20261013/read_accounts.py')
reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)
reader.OUTPUT=reader.ROOT/'funding-start-route-20261013'
original_prepare=reader.prepare

def prepare():
    body,keys,lookup,_=original_prepare()
    cut=json.loads((reader.ROOT/'funding-start-prehistory-20261013/baseline-cut.json').read_bytes())
    request=json.loads(body);request['params'][1]['minContextSlot']=cut['end_slot']
    return reader.A.canonical(request),keys,lookup,cut

reader.prepare=prepare
if __name__=='__main__':reader.main()
