"""Independent analytic compound COM/tensor audit, not a dynamics fidelity claim."""
import json
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[5]
EVIDENCE = ROOT / 'WoW335_ASEC/verification/m3'
fixture = json.loads((EVIDENCE / 'rigid-com-mass-properties.json').read_text(encoding='utf-8-sig'))
assert fixture['ok'] and fixture['rejections'] == 5
assert fixture['maximumGeometryError'] < 1e-5
maximum_center_error = maximum_tensor_error = 0.0
for case in fixture['cases']:
    placement = np.array(case['placement']).reshape((4, 4), order='F')
    principal = np.array(case['centerInBone']).reshape((4, 4), order='F')
    masses = np.array([2.0, 6.0])
    centers = np.array([[-2.0, 0, 0], [2.0, 0, 0]])
    local_center = np.average(centers, axis=0, weights=masses)
    expected_center = placement[:3, :3] @ local_center + placement[:3, 3]
    tensor = np.zeros((3, 3))
    # A solid box of half-extents (1,2,3), plus each child's parallel-axis term.
    for mass, center in zip(masses, centers):
        displacement = center - local_center
        tensor += np.diag(mass / 3 * np.array([13.0, 10.0, 5.0]))
        tensor += mass * (np.eye(3) * np.dot(displacement, displacement) - np.outer(displacement, displacement))
    expected_tensor = placement[:3, :3] @ tensor @ placement[:3, :3].T
    actual_tensor = principal[:3, :3] @ np.diag(case['inertia']) @ principal[:3, :3].T
    center_error = float(np.max(np.abs(expected_center - principal[:3, 3])))
    tensor_error = float(np.max(np.abs(expected_tensor - actual_tensor)))
    assert case['mass'] == float(masses.sum())
    assert center_error < 2e-6 and tensor_error < 2e-4, (case, center_error, tensor_error)
    maximum_center_error = max(maximum_center_error, center_error)
    maximum_tensor_error = max(maximum_tensor_error, tensor_error)

baseline = json.loads((EVIDENCE / 'rigid-com-baseline.json').read_text(encoding='utf-8-sig'))
corpus = json.loads((EVIDENCE / 'rigid-com-corpus.json').read_text(encoding='utf-8-sig'))
assert baseline['failed'] == corpus['failed'] == 0
old = {record['path']: record for record in baseline['files']}
assert len(old) == len(corpus['files']) == 18
for record in corpus['files']:
    assert record['ok']
    for field in ['bodies', 'dynamicBodies', 'joints']:
        assert record[field] == old[record['path']][field]
    assert record['resetPoseError'] < 1e-4 and record['resetGeometryError'] < 1e-4
    assert record['staticPartitionError'] < 1e-4 and record['peakJointPivotError'] < .2
    if 'baseAnimatedAnchorError' in record:
        assert record['baseAnimatedAnchorError'] < 1e-5
report = dict(ok=True, analyticCases=len(fixture['cases']), corpusFiles=len(corpus['files']), maximumCenterError=maximum_center_error, maximumTensorError=maximum_tensor_error, maximumGeometryError=fixture['maximumGeometryError'], maximumResetPoseError=max(r['resetPoseError'] for r in corpus['files']), maximumResetGeometryError=max(r['resetGeometryError'] for r in corpus['files']), scope='Independent solid-box COM/tensor formulas, collision geometry and corpus binding/reset/partition checks; not original-engine mass/dynamics or native visual/FPS proof')
(EVIDENCE / 'rigid-com-reference.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report))
