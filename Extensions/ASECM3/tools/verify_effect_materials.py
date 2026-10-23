"""Compare auxiliary material samples with independently decoded source curves.

This checks ownership and numeric sampling, not native pixels or gameplay FPS.
"""
import hashlib
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'M3_to_M2/tools'))
import convert_m3_to_m2 as c

EVIDENCE = REPO.parent / 'verification/m3'
BANK = EVIDENCE / 'effects-source-dependencies/storm_effect_deathragdolldissipate.m3a'


def load(path):
    document = c.io_m3.M3SectionList.load(str(path), lazy=False)
    return document, document.model


def arr(document, reference):
    return list(c.section_for(document, reference) or [])


def name(document, reference):
    return bytes(arr(document, reference)).rstrip(b'\0').decode('utf-8')


def standard_material(document, model, index):
    reference = arr(document, model.material_references)[index]
    assert reference.type == 1, 'This source audit covers standard materials only'
    return arr(document, model.materials_standard)[reference.material_index]


bank_document, bank_model = load(BANK)
reports = json.loads((EVIDENCE / 'effects-material-overlay-corpus.json').read_text(encoding='utf-8-sig'))
checked = 0
maximum_error = 0.0
files = []
for report in reports:
    assert report['failed'] == 0
    for result in report['files']:
        assert result['ok'] and result['effectMaterialRecords'] == 3
        path = Path(result['path'])
        document, model = load(path)
        owners = set()
        for sample in result['effectMaterialSamples']:
            mesh_material = standard_material(document, model, sample['meshMaterial'])
            bank_material = standard_material(bank_document, bank_model, sample['bankMaterial'])
            assert name(document, mesh_material.name) == sample['material']
            assert name(bank_document, bank_material.name) == sample['material']
            mesh_layer, = arr(document, getattr(mesh_material, sample['role']))
            bank_layer, = arr(bank_document, getattr(bank_material, sample['role']))
            assert mesh_layer.desc.version == bank_layer.desc.version
            # Verify the entire source reference signature, not just the active ID.
            for field in mesh_layer.desc.fields:
                mesh_value, bank_value = getattr(mesh_layer, field), getattr(bank_layer, field)
                if hasattr(mesh_value, 'header'):
                    assert mesh_value.desc.history.name == bank_value.desc.history.name
                    assert mesh_value.header.id == bank_value.header.id
            source_reference = getattr(mesh_layer, sample['field'])
            auxiliary_reference = getattr(bank_layer, sample['field'])
            assert source_reference.header.id == sample['id'] == auxiliary_reference.header.id
            assert source_reference.header.interpolation == auxiliary_reference.header.interpolation
            assert sample['type'] == 5
            clip = arr(bank_document, bank_model.sequences)[sample['sequence']]
            duration = clip.anim_ms_end - clip.anim_ms_start
            offset = min(sample['elapsed'], duration) if clip.flags & 1 else sample['elapsed'] % duration if duration else 0
            source_time = clip.anim_ms_start + offset
            group = arr(bank_document, bank_model.sequence_transformation_groups)[sample['sequence']]
            channels = {}
            priorities = {}
            for index in arr(bank_document, group.stc_indices):
                collection = arr(bank_document, bank_model.sequence_transformation_collections)[index]
                for channel_id, encoded in zip(arr(bank_document, collection.anim_ids), arr(bank_document, collection.anim_refs)):
                    if encoded >> 16 != 5:
                        continue
                    entry = arr(bank_document, collection.sdr3)[encoded & 65535]
                    keys = dict(zip(arr(bank_document, entry.frames), arr(bank_document, entry.keys)))
                    if channel_id not in priorities or collection.priority >= priorities[channel_id]:
                        priorities[channel_id] = collection.priority
                        channels[channel_id] = dict(type=5, timestamps=tuple(sorted(keys)), values=tuple((float(keys[t]),) for t in sorted(keys)))
            initial = (float(source_reference.default),)
            expected = c._sample_m3_track(channels.get(sample['id']), source_time, initial, source_reference.header.interpolation, False)[0]
            assert math.isclose(sample['initial'][0], initial[0], rel_tol=3e-6, abs_tol=3e-6)
            error = abs(sample['value'][0] - expected)
            maximum_error = max(maximum_error, error)
            assert math.isclose(sample['value'][0], expected, rel_tol=3e-6, abs_tol=3e-6), (path, sample, expected)
            owners.add((sample['meshMaterial'], sample['bankMaterial'], sample['role'], sample['field'], sample['id']))
            checked += 1
        assert len(owners) == 3
        files.append(dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(), owners=len(owners), samples=len(result['effectMaterialSamples'])))
output = dict(ok=True, files=files, samples=checked, maximumError=maximum_error, bankSHA256=hashlib.sha256(BANK.read_bytes()).hexdigest(), scope='Independent source curve and owner/reference validation; no native rendering, pixel, malformed-input or FPS proof.')
(EVIDENCE / 'effects-material-overlay-reference.json').write_text(json.dumps(output, indent=2) + '\n', encoding='utf-8')
print(json.dumps(output))
