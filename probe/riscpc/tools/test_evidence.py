#!/usr/bin/env python3
"""Authored evidence failures: truncated runs, counter thresholds, phase drift."""
import csv
import json
from pathlib import Path
import tempfile
import unittest
from evidence import completion, core_rows, digest, EvidenceError
from evaluate_ac8 import evaluate
from label_phases import label
from evaluate_suite import suite
from compare_traces import check as check_trace

HEADER=('frame','emu_ns','period_ns','exec_ns','instructions','audio_frames','underruns','overruns','video_hash','audio_hash','wall_ns','instrument_ns','status')

class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.p=Path(self.tmp.name)
        # 1s authored records simplify percentile and exact 30min window tests.
        self.rows=[dict(zip(HEADER,(i,(i+1)*10**9,10**9,500000000,100,48000,0,0,'1234567890abcdef','1234567890abcdef',(i+1)*10**9,100,'OK'))) for i in range(1800)]
        self.csv=self.p/'frames.csv';self.write()
        (self.p/'csv-cost.csv').write_text('frame,csv_append_ns\n'+''.join(str(i)+',0\n' for i in range(1800)))
        self.footer=dict(status='COMPLETE',frames=1800,requested_ns=1800*10**9,advanced_ns=1800*10**9,reset=0,measurement=1,cost_accounting='deferred-csv-tail-v1',video_renderer='line-v4')
        self.footer_path=self.p/'run-footer.json';self.save_footer()
        self.script=self.p/'script.txt';self.script.write_text('0 checkpoint 0 0 origin\n')
        self.events=self.p/'events.csv';self.events.write_text('ordinal,scheduled_ns,delivered_ns,event,a,b,video_hash,pc\n1,0,0,checkpoint,0,0,1234567890abcdef,00000000\n')
        self.declaration=self.p/'declaration.json'
        self.declaration.write_text(json.dumps(dict(kind='sustained',measurement_origin_ns=0,requested_ns=1800*10**9,predeclared_exclusions=[],phases=[dict(start_ns=0,end_ns=1800*10**9+1,phase='active')],script_sha256=digest(self.script))))
        self.phases=self.p/'phases.csv';label(self.csv,self.declaration,self.script,self.events,self.phases)
        self.manifest=self.p/'manifest.json';self.save_manifest()
    def tearDown(self):self.tmp.cleanup()
    def write(self):
        with self.csv.open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=HEADER);w.writeheader();w.writerows(self.rows)
    def save_footer(self):self.footer_path.write_text(json.dumps(self.footer))
    def save_manifest(self):
        self.manifest.write_text(json.dumps({k:dict(raw_file=p.name,sha256=digest(p)) for k,p in [('declaration',self.declaration),('footer',self.footer_path),('phases',self.phases)]}))
    def evaluate(self):return evaluate(self.csv,self.manifest)['criteria']
    def test_complete_core_footer_and_missing_telemetry(self):
        self.assertEqual(completion(self.csv,core_rows(self.csv))['frames'],1800)
        result=self.evaluate();self.assertEqual(result['p95'],'PASS');self.assertEqual(result['matched_phase_last_first'],'PASS')
        for k in ('frontend_missed_presentations','independent_audio_output','platform_conditions','active_gameplay','instrumentation_overhead'):self.assertEqual(result[k],'MISSING')
    def test_final_renderer_required(self):
        self.footer.pop('video_renderer');self.save_footer();self.save_manifest()
        self.assertEqual(self.evaluate()['run_integrity'],'MISSING')
        for version in ('line-v1','line-v2','line-v3'):
            self.footer['video_renderer']=version;self.save_footer();self.save_manifest()
            self.assertEqual(self.evaluate()['run_integrity'],'MISSING')
    def test_last_row_is_not_completion(self):
        self.footer_path.unlink()
        with self.assertRaises(EvidenceError):completion(self.csv,core_rows(self.csv))
    def test_count_mismatch_and_failed_footer(self):
        self.footer['frames']=1799;self.save_footer()
        with self.assertRaises(EvidenceError) as c:completion(self.csv,core_rows(self.csv))
        self.assertEqual(c.exception.status,'FAIL')
        self.footer['status']='FAIL';self.save_footer()
        with self.assertRaises(EvidenceError):completion(self.csv,core_rows(self.csv))
    def test_wrong_request_rejected(self):
        with self.assertRaises(EvidenceError):completion(self.csv,core_rows(self.csv),expected_ns=1801*10**9)
    def test_reset_gap_and_truncated_row(self):
        self.rows[5]['frame']=7;self.write()
        with self.assertRaises(EvidenceError):core_rows(self.csv)
        self.rows[5]['frame']=5;self.rows[5]['wall_ns']='';self.write()
        with self.assertRaises(EvidenceError):core_rows(self.csv)
    def test_explicit_failed_csv(self):
        self.rows[5]['status']='FAIL';self.write()
        with self.assertRaises(EvidenceError) as c:core_rows(self.csv)
        self.assertEqual(c.exception.status,'FAIL')
    def test_three_consecutive_deadlines_and_audio(self):
        for i in range(700,703):self.rows[i]['exec_ns']=1100000000
        self.rows[800]['underruns']=1;self.write();r=self.evaluate()
        self.assertEqual(r['consecutive_deadline_misses'],'FAIL');self.assertEqual(r['core_audio'],'FAIL')
    def test_boundary_threshold_and_phase_regression(self):
        for r in self.rows[600:]:r['exec_ns']=800000000
        self.write();self.assertEqual(self.evaluate()['p95'],'PASS')
        for r in self.rows[1500:]:r['exec_ns']=900000000
        self.write();r=self.evaluate();self.assertEqual(r['p95'],'FAIL');self.assertEqual(r['matched_phase_last_first'],'FAIL')
    def test_phase_tamper_and_late_events(self):
        text=self.phases.read_text().replace('active','other',1);self.phases.write_text(text);self.save_manifest()
        self.assertEqual(self.evaluate()['matched_phase_last_first'],'MISSING')
        self.events.write_text(self.events.read_text().replace('1,0,0,','1,0,1000,'))
        with self.assertRaises(EvidenceError):label(self.csv,self.declaration,self.script,self.events,self.phases)
    def test_negative_and_nonnumeric_measurement(self):
        for value in (-1,'nan'):
            self.rows[5]['exec_ns']=value;self.write()
            with self.assertRaises(EvidenceError):core_rows(self.csv)
    def test_empty_suite_never_qualifies(self):
        path=self.p/'suite.json';path.write_text('{}')
        out=suite(path)['criteria']
        self.assertEqual(out['independent_runs'],'MISSING')
        self.assertEqual(out['frozen_calibration'],'MISSING')
        self.assertTrue(all(s=='MISSING' for k,s in out.items() if k.startswith(('sustained_','screening_'))))
        for platform in ('windows','odin3'):
            for payload in ('trial-c-1','trial-c-2'):
                self.assertEqual(out['sustained_'+platform+'_'+payload],'MISSING')
    def test_duplicate_raw_cannot_count_as_independent_runs(self):
        path=self.p/'suite.json'
        run=dict(csv=self.csv.name,manifest=self.manifest.name,kind='sustained',platform='windows',workload='trial-a',model='SA110',rate=100)
        path.write_text(json.dumps(dict(runs=[dict(run,repetition=i) for i in (1,2,3)],frozen_model_rates={'SA110':100},calibration_qualified=True)))
        self.assertEqual(suite(path)['criteria']['independent_runs'],'FAIL')
    def test_replay_missing_and_invalid_duration(self):
        import subprocess, sys
        tool=Path(__file__).with_name('compare_replay.py')
        for minimum in ('nan','inf','0','599'):
            run=subprocess.run([sys.executable,str(tool),str(self.p/'absent'),str(self.p/'absent2'),'--minimum-seconds',minimum],capture_output=True,text=True)
            self.assertEqual(run.returncode,2)
            self.assertIn('at least600',run.stderr)
        run=subprocess.run([sys.executable,str(tool),str(self.p/'absent'),str(self.p/'absent2')],capture_output=True,text=True)
        self.assertEqual(run.returncode,2)
        self.assertTrue(run.stdout.startswith('MISSING'))

    def test_raster_completion_in_stderr_is_required(self):
        trace=self.p/'raster.trace';errors=self.p/'raster.stderr'
        trace.write_text('model=0\nmodel=1\nmodel=2\nmodel=3\n')
        with self.assertRaises(ValueError):check_trace(trace)
        errors.write_text('raster checks=360 failures=0\n')
        self.assertEqual(check_trace(trace,errors)[1]['checks'],360)
        errors.write_text('FAIL split\nraster checks=360 failures=0\n')
        with self.assertRaises(ValueError):check_trace(trace,errors)

if __name__=='__main__':unittest.main()
