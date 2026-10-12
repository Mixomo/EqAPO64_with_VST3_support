"""Join discovery and bounded evidence without promoting partial checks to PASS."""
import argparse
import json
import re
from pathlib import Path
from datetime import datetime, timezone

REQUIRED = ['scan', 'instantiate', 'query_io', 'sample_rates', 'block_sizes',
            'precision', 'activate', 'process_audio', 'change_parameters',
            'verify_parameter_pcm', 'open_gui', 'gui_animation', 'resize_gui',
            'close_gui', 'dsp_after_gui_close', 'bypass', 'unbypass', 'save_state',
            'restore_state', 'reload', 'unload', 'instantiate_again',
            'analyzer_state_update', 'crash_recovery', 'shutdown_cleanly',
            'live_dpi', 'installed_audiodg']


def read(path, default):
    return json.loads(path.read_text(encoding='utf-8-sig')) if path.exists() else default


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--revision', type=Path, required=True)
    args = parser.parse_args()
    root = args.revision
    inventory = read(root / 'inventory/discovered-plugins.json', [])
    partial = {}
    # Campaign evidence supersedes historical focused evidence for the same key.
    for directory in ('focused-filters', 'all-filters', 'focused-filters-v2'):
        for row in read(root / 'results' / directory / 'matrix.json', []):
            partial[(row['path'].casefold(), row['mode'])] = (row, directory)
    gui = {}
    for directory, mode in [('goodhertz-qt-family', 'INPROC'),
                            ('goodhertz-native-family', 'OUTPROC')]:
        for row in read(root / 'results' / directory / 'summary.json', []):
            gui[(row['plugin'].casefold(), mode)] = (row, directory)
    plugins = []
    publication_isolation = {}
    native_dsp_views = {}
    for result in read(root / 'results/native-dsp-view-matrix-supplement.json', []):
        native_dsp_views[result['plugin'].casefold()] = result
    for result in read(root / 'results/publication-control-allocation-after/summary.json', []):
        publication_isolation[result['plugin'].casefold()] = result
    rate_blocks = {}
    for directory in ('fabfilter-rate-block-grid', 'fabfilter-rate-block-independent-quantum'):
        for result in read(root / 'results' / directory / 'summary.json', {}).get('cases', []):
            case = dict(result)
            case['evidence'] = f'results/{directory}/{result["evidence"]}'
            case['transport'] = read(root / (case['evidence'] + '.transport.json'), [])
            rate_blocks.setdefault((result['plugin'].casefold(), result['mode']), {})[
                (result['sample_rate'], result['block_size'])] = case
    row_release = {}
    for result in read(root / 'results/editor-row-release-fixed-family/summary.json', {}).get('cases', []):
        row_release[result['plugin'].casefold()] = result
    analyzer_static = {}
    for directory in ('analyzer-shape-regression-batch', 'analyzer-q4-shelves-cuts',
                      'analyzer-q3-shelves-cuts'):
        folder = root / 'results' / directory
        summary = read(folder / 'summary.json', {})
        for result in summary.get('cases', []):
            config = folder / (result['case'] + '.txt')
            if not config.exists():
                continue
            match = re.match(r'(VSTPlugin|OutProcVSTPlugin): Library "([^"]+)"',
                             config.read_text(encoding='utf-8-sig'))
            if not match:
                continue
            mode = 'INPROC' if match[1] == 'VSTPlugin' else 'OUTPROC'
            evidence = folder / result['evidence']
            if not evidence.exists():
                continue
            analyzer_static.setdefault((match[2].casefold(), mode), []).append({
                'case': result['case'], 'result': result['status'],
                'evidence': evidence.relative_to(root).as_posix(),
                'limits': summary.get('limits')})
    protected_close = {}
    close_batch = read(root / 'results/goodhertz-protected-close-batch/summary.json', {})
    for result in close_batch.get('entries', []):
        protected_close[result['plugin'].casefold()] = result
    live_gui = {}
    lifecycle = {}
    editor_rows = {}
    for path in sorted((root / 'results').glob('editor-row-live-*/bounded-result.json')):
        result = read(path, {})
        if result.get('status') != 'PASS' or not result.get('owned_real_mouse_click'):
            continue
        key = (result['plugin'].casefold(), result['mode'])
        previous = editor_rows.get(key)
        if previous and previous[0].get('analyzer_gui_update_verified') and not result.get('analyzer_gui_update_verified'):
            continue
        if previous and 'restored_ratio' in previous[0] and 'restored_ratio' not in result:
            continue
        editor_rows[key] = (result, path.relative_to(root).as_posix())
    for path in sorted((root / 'results').glob('lifecycle-q*/summary.json')):
        result = read(path, {})
        if result.get('status') == 'PASS' and result.get('verified_cycles') == 100:
            lifecycle[(result['plugin'].casefold(), result['mode'])] = (result, path.relative_to(root).as_posix())
    for path in sorted((root / 'results').glob('real-gui-live-*/result.json')):
        result = read(path, {})
        if result.get('status') != 'PASS':
            continue
        mode = 'INPROC' if result.get('mode') == 'IN_PROCESS' else 'OUTPROC'
        key = (result['plugin'].casefold(), mode)
        existing = live_gui.get(key)
        if existing and existing[0].get('verified_audio_after_gui_close') and not result.get('verified_audio_after_gui_close'):
            continue
        live_gui[key] = (result, path.relative_to(root).as_posix())
    for inventory_index, entry in enumerate(inventory):
        key = entry['path'].casefold()
        record = {name: entry.get(name) for name in
                  ('path', 'type', 'vendor', 'product', 'version', 'architecture',
                   'vendor_detection', 'version_resources', 'scanner_result')}
        record['vst_classes'] = entry.get('scanner_metadata', {}).get('classes', [])
        record['metadata'] = {'unique_id': None, 'input_channels': None,
                              'output_channels': None, 'input_buses': None,
                              'output_buses': None, 'float32': None, 'float64': None,
                              'latency_by_mode': {}, 'has_state': None,
                              'editor_available': None}
        modes = {}
        for mode in ('INPROC', 'OUTPROC'):
            evidence = []
            checks = {name: {'status': 'NOT_VERIFIED'} for name in REQUIRED}
            checks['scan'] = {'status': 'FACTORY_SCAN_ONLY', 'result': entry.get('scanner_result')}
            assessment = 'NOT_EVALUATED'
            observed = None
            if (key, mode) in partial:
                row, directory = partial[(key, mode)]
                assessment = 'PARTIAL_EVALUATION'
                observed = {'status': row['status'], 'reason': row.get('reason'),
                            'exit_code': row.get('exit_code'), 'seconds': row.get('seconds')}
                probe = row.get('probe', {})
                evidence.append(f'results/{directory}/matrix.json')
                if probe:
                    checks['process_audio'] = {'status': 'BOUNDED_FILTER_PCM_ONLY',
                                               'result': probe.get('status'),
                                               'limits': probe.get('limits')}
                    if directory == 'focused-filters-v2' and mode == 'OUTPROC':
                        transport_path = root / 'results' / directory / f'{inventory_index:04d}-{mode}.json.transport.json'
                        transport = read(transport_path, [])
                        if transport:
                            evidence.append(transport_path.relative_to(root).as_posix())
                            checks['outproc_dsp_progress'] = {
                                'status': 'TWO_BOUNDED_PCM_MEASUREMENTS_WITH_CONSUMED_SLOTS',
                                'snapshots': transport,
                                'limits': 'Progress counters only; not full continuity, performance or installed audiodg certification'}
                    checks['query_io'] = {'status': 'CHANNEL_COUNTS_ONLY',
                                         'input_channels': probe.get('inputs'),
                                         'output_channels': probe.get('outputs')}
                    record['metadata'].update(input_channels=probe.get('inputs'),
                                              output_channels=probe.get('outputs'),
                                              float64=probe.get('float64'))
                    if entry['type'] == 'VST2':
                        record['metadata']['unique_id'] = probe.get('unique_id')
                    record['metadata']['latency_by_mode'][mode] = probe.get('edited_latency')
                    if 'pro-q' in key and probe.get('status') == 'PASS':
                        checks['verify_parameter_pcm'] = {'status': 'ONE_SAVED_GAIN_EDIT_VERIFIED',
                                                          'rms_delta': probe.get('rms_delta')}
            if (key, mode) in gui:
                result, directory = gui[(key, mode)]
                evidence.append(f'results/{directory}/summary.json')
                checks['close_gui'] = {'status': 'ONE_ISOLATED_CYCLE', 'result': result['status']}
                checks['open_gui'] = {'status': 'ONE_ISOLATED_CYCLE', 'result': result['status']}
                if result['status'] == 'PASS':
                    record['metadata']['editor_available'] = True
            if (key, mode) in live_gui:
                result, path = live_gui[(key, mode)]
                evidence.append(path)
                checks['change_parameters'] = {'status': 'ONE_NATIVE_GUI_EDIT', 'parameter': result['parameter']}
                checks['verify_parameter_pcm'] = {'status': 'PERSISTENT_DSP_GUI_EDIT_PCM_VERIFIED',
                                                  'ratio': result['audio']['ratio'],
                                                  'generation': result['audio']['parameter_generation'],
                                                  'sequence': result['audio']['parameter_sequence'],
                                                  'limits': result['limits']}
                if result.get('verified_audio_after_gui_close'):
                    checks['dsp_after_gui_close'] = {'status': 'ONE_NATIVE_GUI_CLOSE_PCM_VERIFIED',
                                                     'rms_ratio': result['audio']['after_gui_close_ratio']}
            if (key, mode) in lifecycle:
                result, path = lifecycle[(key, mode)]
                evidence.append(path)
                for name in ('bypass', 'unbypass', 'reload', 'unload', 'instantiate_again'):
                    checks[name] = {'status': '100_GUI_CLOSED_GRAPH_CYCLES_PCM_VERIFIED',
                                    'cycles': result['verified_cycles'], 'limits': result['limits']}
            if (key, mode) in editor_rows:
                result, path = editor_rows[(key, mode)]
                evidence.append(path)
                checks['editor_dialog_parameter_pcm'] = {'status': 'ONE_ACTUAL_QT_ROW_EDIT_PCM_VERIFIED',
                                                         'parameter': result['parameter'], 'rms_ratio': result['ratio'],
                                                         'sequence': result['parameter_sequence'], 'limits': result['limits']}
                if 'restored_ratio' in result:
                    checks['restore_state'] = {'status': 'ONE_QT_SAVED_ROW_FRESH_FILTER_PCM_VERIFIED',
                                               'rms_ratio': result['restored_ratio'], 'limits': result['limits']}
                if result.get('analyzer_gui_update_verified'):
                    checks['analyzer_row_gui_state_update'] = {
                        'status': 'ONE_ACTUAL_ROW_GUI_EDIT_CLONE_CURVE_VERIFIED',
                        'delta_db': result['analyzer_delta_db'], 'limits': result['limits']}
            if mode == 'OUTPROC' and key in protected_close:
                result = protected_close[key]
                evidence.append('results/goodhertz-protected-close-batch/summary.json')
                checks['protected_qt_row_shutdown'] = {
                    'status': 'ONE_ACTUAL_PROTECTED_QT_ROW_CLOSE', 'result': result['status'],
                    'forced': result.get('shutdown_forced'),
                    'close_call_ms': result.get('close_call_ms'),
                    'shutdown_elapsed_ms': result.get('shutdown_elapsed_ms'),
                    'max_qt_tick_gap_ms': result.get('max_qt_tick_gap_ms'),
                    'limits': result.get('limits')}
            if (key, mode) in analyzer_static:
                cases = analyzer_static[(key, mode)]
                evidence.extend(case['evidence'] for case in cases)
                checks['analyzer_static_response'] = {
                    'status': 'BOUNDED_STATIC_CURVES_ONLY', 'cases': cases,
                    'live_state_updates_verified': False}
            if mode == 'INPROC' and key in row_release:
                result = row_release[key]
                evidence.append('results/editor-row-release-fixed-family/summary.json')
                checks['qt_row_instance_release'] = {
                    'status': 'FIVE_SAME_PROCESS_ROW_GUI_LIFETIMES', 'result': result['status'],
                    'cycles': result['cycles'], 'limits': result.get('limits')}
            if (key, mode) in rate_blocks:
                cases = list(rate_blocks[(key, mode)].values())
                evidence.extend(case['evidence'] for case in cases)
                checks['sample_rates_and_blocks_pcm_grid'] = {
                    'status': 'BOUNDED_GRID_WITH_EXPLICIT_FAILURES', 'cases': cases,
                    'limits': 'Automatic plugin precision; not complete lifecycle, GUI or installed audiodg'}
            if mode == 'OUTPROC' and key in publication_isolation:
                result = publication_isolation[key]
                evidence.append('results/publication-control-allocation-after/summary.json')
                checks['parameter_replay_allocation_isolation'] = {
                    'status': 'ONE_CONTROL_ALLOCATION_FAULT_PCM_TRIAL', 'result': result['status'],
                    'control_allocation_delayed': result['control_allocation_delayed'],
                    'published': result['published'], 'rms': result['rms'],
                    'expected_rms': result['expected_rms'], 'limits': result['limits']}
            if mode == 'OUTPROC' and key in native_dsp_views:
                result = native_dsp_views[key]
                evidence.append(result['evidence'])
                checks['native_view_on_actual_dsp_trial'] = {
                    'status': 'INTERACTIVE_PRIVATE_HOST_TRIAL', 'result': result['status'],
                    'real_component_view_attached': result['real_component_view_attached'],
                    'view_detached_before_final_pcm': result['view_detached_before_final_pcm'],
                    'native_meter_verified': result['native_meter_verified'],
                    'after_close_pcm_ratio': result['pcm']['after_gui_close_ratio'],
                    'limits': result['limits']}
            modes[mode] = {'evaluation_status': assessment, 'overall_status': 'INCOMPLETE',
                           'compatibility_classification': None,
                           'observed_probe_result': observed, 'checks': checks,
                           'evidence': evidence,
                           'remaining_requirements': REQUIRED.copy()}
        record['modes'] = modes
        plugins.append(record)
    summary = {'discovered_plugins': len(plugins), 'mode_records': len(plugins) * 2,
               'partially_evaluated_modes': sum(mode['evaluation_status'] == 'PARTIAL_EVALUATION'
                                               for row in plugins for mode in row['modes'].values()),
               'full_compatibility_passes': {'INPROC': 0, 'OUTPROC': 0}}
    report = {'schema_version': 1, 'generated_utc': datetime.now(timezone.utc).isoformat(),
              'release_status': 'NOT_CERTIFIED', 'summary': summary,
              'required_rates': [44100, 48000, 96000],
              'required_blocks': [32, 64, 128, 256, 480, 512, 960],
              'limitations': ['Factory scan does not prove initialization or DSP.',
                              'Original all-filters finite-output smoke does not prove OOP wet processing.',
                              'Null metadata is unknown, not unsupported.',
                              'GUI closure does not prove animation or live GUI-to-DSP parameters.',
                              'All explicit requirements remain unverified unless their full scope is covered.'],
              'plugins': plugins}
    destination = root / 'PLUGIN_COMPATIBILITY_MATRIX.json'
    temporary = destination.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(report, indent=2), encoding='utf-8')
    temporary.replace(destination)
    def cell(value):
        return str(value or '').replace('|', '\\|').replace('\n', ' ')
    markdown = ['# Revision2 compatibility evidence', '',
                'Work in progress. No full plugin compatibility PASS is certified.', '',
                f"Discovered plugins: {len(plugins)}; partial mode evaluations: {summary['partially_evaluated_modes']}.", '',
                '| Plugin / path | Vendor | Format | INPROC observation | OUTPROC observation |',
                '| --- | --- | --- | --- | --- |']
    for row in plugins:
        observations = []
        for mode in ('INPROC', 'OUTPROC'):
            observed = row['modes'][mode]['observed_probe_result']
            observations.append('NOT_EVALUATED' if not observed else
                                observed['status'] + (': ' + observed['reason'] if observed['reason'] else ''))
        markdown.append('| ' + ' | '.join(cell(value) for value in
                         [row['path'], row['vendor'], row['type'], *observations]) + ' |')
    markdown.extend(['', 'Full checks, unknown metadata and evidence references are recorded in the JSON.',
                     'Observed failure reasons do not establish host/plugin/license causation without diagnostics.'])
    (root / 'PLUGIN_COMPATIBILITY_MATRIX.md').write_text('\n'.join(markdown) + '\n', encoding='utf-8')
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
