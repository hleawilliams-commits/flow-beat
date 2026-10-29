// SPDX-License-Identifier: GPL-3.0-only
// Synthetic value-only fixtures. No paths, environment, filesystem, transport,
// process, native backend, or device calls. Producer receipts are synthetic:
// using the actual value type does not authenticate a kernel or native poll.
use super::*;
use crate::mixer_producer::{BaselineReceipt, Frame, FrameAck, Key, Producer};

// Independently enumerated from the pinned writer-boundaries review, without
// consulting mapper offset helpers or decoder internals.
const ANALOG: [usize; 51] = [
    10, 11, 12, 13, 14, 15, 16, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
    53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76,
    77, 78, 80,
];
const NONE: Selection = Selection {
    selector: false,
    fx_time: false,
};
const ALL: Selection = Selection {
    selector: true,
    fx_time: true,
};

fn acknowledged(frame: Frame) -> FrameAck {
    acknowledged_n(frame, 1, 0)
}

fn acknowledged_n(frame: Frame, count: u64, salt: u64) -> FrameAck {
    let baseline = BaselineReceipt {
        key: Key {
            instance: [0x1234 + salt, 0x5678 + salt],
            run_id: 7 + salt,
            epoch: 11 + salt,
            producer_id: 13 + salt,
        },
        frame: [0x5a; 152],
        kernel_sequence: 23 + salt,
    };
    let producer = Producer::new(2).unwrap();
    producer.stage_baseline(baseline).unwrap();
    producer.acknowledge_baseline(baseline).unwrap();
    producer.confirm_released(baseline, true).unwrap();
    let mut latest = None;
    for index in 1..=count {
        let request = producer.create_for_key(baseline.key, frame).unwrap();
        assert_eq!(producer.admit(request).unwrap(), request);
        let ack = FrameAck {
            request,
            kernel_sequence: baseline.kernel_sequence + index,
        };
        producer.acknowledge_frame(ack).unwrap();
        latest = Some(ack);
    }
    latest.expect("nonzero synthetic request count")
}

fn canary() -> Frame {
    std::array::from_fn(|i| (i as u8).wrapping_mul(73).wrapping_add(19))
}

fn analog_pairs(frame: &Frame) -> Vec<(usize, u8)> {
    ANALOG
        .iter()
        .map(|&offset| (offset, frame[offset]))
        .collect()
}

// Independent output oracle; no production parser or map function is used here.
fn encode(pairs: &[(usize, u8)]) -> Vec<u8> {
    let mut sorted = pairs.to_vec();
    sorted.sort_unstable_by_key(|&(offset, _)| offset);
    let fields: Vec<_> = sorted
        .iter()
        .map(|(offset, value)| format!("\"{}\": {}", offset, value))
        .collect();
    format!("{{{}}}", fields.join(", ")).into_bytes()
}

fn supported(offset: usize) -> bool {
    ANALOG.contains(&offset) || [22, 26, 27].contains(&offset)
}

fn mapped(frame: Frame, saved: Option<&[u8]>, selected: Selection) -> MappedSettings {
    map(acknowledged(frame), saved, selected).unwrap()
}

#[test]
fn canary_copies_all_analog_bytes_and_only_explicit_optional_controls() {
    let frame = canary();
    let ack = acknowledged(frame);
    let result = map(ack, None, ALL).unwrap();
    let mut expected = analog_pairs(&frame);
    expected.extend([(22, frame[22] & 0xf0), (26, frame[26]), (27, frame[27])]);
    assert_eq!(
        result.targets().mixer,
        encode(&expected),
        "all bytes use the exact ACK frame"
    );
    assert_eq!(result.targets().knob, frame[34].to_string().as_bytes());
    assert_eq!(result.ack(), &ack);
}

#[test]
fn all_256_values_preserve_every_analog_shared_bit() {
    for value in 0..=u8::MAX {
        let frame = [value; 152];
        let result = mapped(frame, None, NONE);
        let expected: Vec<_> = ANALOG.iter().map(|&offset| (offset, value)).collect();
        assert_eq!(
            result.targets().mixer,
            encode(&expected),
            "full byte {}",
            value
        );
    }
}

#[test]
fn knob_counter_uses_byte_34_without_sign_mask_newline_or_other_byte() {
    for value in 0..=u8::MAX {
        let mut frame = [0x5a; 152];
        frame[33] = value.wrapping_add(1);
        frame[34] = value;
        frame[35] = value.wrapping_sub(1);
        let result = mapped(frame, None, NONE);
        assert_eq!(
            result.targets().knob,
            value.to_string().into_bytes(),
            "counter {}",
            value
        );
    }
}

#[test]
fn all_four_selections_preserve_or_create_each_valid_optional_group() {
    let frame = canary();
    for selector_selected in [false, true] {
        for time_selected in [false, true] {
            for selector_present in [false, true] {
                for time_present in [false, true] {
                    let mut old = vec![(10, frame[10].wrapping_add(1))];
                    if selector_present {
                        old.push((22, 0x40));
                    }
                    if time_present {
                        old.extend([(26, 0xa1), (27, 0xb2)]);
                    }
                    let bytes = encode(&old);
                    let selected = Selection {
                        selector: selector_selected,
                        fx_time: time_selected,
                    };
                    let result = mapped(frame, Some(&bytes), selected);
                    let mut expected = analog_pairs(&frame);
                    if selector_selected {
                        expected.push((22, frame[22] & 0xf0));
                    } else if selector_present {
                        expected.push((22, 0x40));
                    }
                    if time_selected {
                        expected.extend([(26, frame[26]), (27, frame[27])]);
                    } else if time_present {
                        expected.extend([(26, 0xa1), (27, 0xb2)]);
                    }
                    assert_eq!(
                        result.targets().mixer,
                        encode(&expected),
                        "selection {:?}, old selector {}, old time {}",
                        selected,
                        selector_present,
                        time_present
                    );
                }
            }
        }
    }
}

#[test]
fn prior_analog_values_are_replaced_from_ack_even_when_optional_controls_are_off() {
    let frame = canary();
    let old: Vec<_> = ANALOG
        .iter()
        .rev()
        .map(|&offset| (offset, frame[offset].wrapping_add(1)))
        .collect();
    let result = mapped(frame, Some(&encode(&old)), NONE);
    assert_eq!(result.targets().mixer, encode(&analog_pairs(&frame)));
}

#[test]
fn selected_selector_strips_all_transient_low_nibble_patterns() {
    for byte in 0..=u8::MAX {
        let mut frame = [0; 152];
        frame[22] = byte;
        let result = mapped(
            frame,
            None,
            Selection {
                selector: true,
                fx_time: false,
            },
        );
        let mut expected = analog_pairs(&frame);
        expected.push((22, byte & 0xf0));
        assert_eq!(
            result.targets().mixer,
            encode(&expected),
            "selector byte {}",
            byte
        );
    }
}

#[test]
fn unsafe_prior_selector_is_refused_before_any_selected_overwrite() {
    for byte in 0..=u8::MAX {
        if byte & 0x0f == 0 {
            continue;
        }
        let saved = format!("{{\"22\": {}}}", byte);
        for selection in [NONE, ALL] {
            assert_eq!(
                map(acknowledged(canary()), Some(saved.as_bytes()), selection),
                Err(MapError::SelectorMomentaryBits),
                "prior selector {}",
                byte
            );
        }
    }
}

#[test]
fn signed_time_patterns_remain_exact_little_endian_bytes() {
    for value in [i16::MIN, -32767, -257, -256, -1, 0, 1, 255, 256, i16::MAX] {
        let [low, high] = value.to_le_bytes();
        let mut frame = [0; 152];
        frame[26] = low;
        frame[27] = high;
        let result = mapped(
            frame,
            Some(b"{\"26\": 99, \"27\": 66}"),
            Selection {
                selector: false,
                fx_time: true,
            },
        );
        let mut expected = analog_pairs(&frame);
        expected.extend([(26, low), (27, high)]);
        assert_eq!(
            result.targets().mixer,
            encode(&expected),
            "signed TIME {}",
            value
        );
    }
}

#[test]
fn incomplete_saved_time_is_refused_even_when_time_would_be_overwritten() {
    for lone_offset in [26, 27] {
        for value in [0, 255] {
            let saved = format!("{{\"{}\": {}}}", lone_offset, value);
            for selector in [false, true] {
                for fx_time in [false, true] {
                    assert_eq!(
                        map(
                            acknowledged(canary()),
                            Some(saved.as_bytes()),
                            Selection { selector, fx_time }
                        ),
                        Err(MapError::IncompleteFxTime),
                        "lone TIME offset {}",
                        lone_offset
                    );
                }
            }
        }
    }
}

#[test]
fn changing_each_nonpersistent_byte_cannot_change_targets() {
    let frame = canary();
    let expected = mapped(frame, None, ALL).targets().clone();
    for offset in 0..152 {
        if supported(offset) || offset == 34 {
            continue;
        }
        let mut changed = frame;
        changed[offset] ^= 0xff;
        assert_eq!(
            mapped(changed, None, ALL).targets(),
            &expected,
            "excluded offset {}",
            offset
        );
    }
}

#[test]
fn every_unsupported_in_frame_offset_is_rejected() {
    for offset in 0..152 {
        if supported(offset) {
            continue;
        }
        let saved = format!("{{\"{}\": 0}}", offset);
        assert_eq!(
            map(acknowledged(canary()), Some(saved.as_bytes()), ALL),
            Err(MapError::UnsupportedKey),
            "unsupported offset {}",
            offset
        );
    }
}

#[test]
fn out_of_frame_and_overflowed_offsets_are_refused() {
    for key in [
        "152",
        "153",
        "254",
        "255",
        "256",
        "65535",
        "18446744073709551615",
        "18446744073709551616",
        "999999999999999999999999999999999999999999999999",
    ] {
        let saved = format!("{{\"{}\": 1}}", key);
        assert!(
            map(acknowledged(canary()), Some(saved.as_bytes()), ALL).is_err(),
            "key {}",
            key
        );
    }
}

#[test]
fn every_supported_duplicate_key_is_refused_before_overwrite() {
    for offset in 0..152 {
        if !supported(offset) {
            continue;
        }
        let saved = format!("{{\"{}\": 0, \"{}\": 1}}", offset, offset);
        assert_eq!(
            map(acknowledged(canary()), Some(saved.as_bytes()), ALL),
            Err(MapError::DuplicateKey),
            "duplicate key {}",
            offset
        );
    }
}

#[test]
fn noncanonical_alias_keys_cannot_evade_duplicate_or_supported_checks() {
    for key in [
        "010",
        "+10",
        "-10",
        " 10",
        "10 ",
        "",
        "1e1",
        "10.0",
        "0x0a",
        "\\u0031\\u0030",
        "\\u000a",
        "１0",
        "١٠",
    ] {
        let saved = format!("{{\"10\": 1, \"{}\": 2}}", key);
        assert!(
            map(acknowledged(canary()), Some(saved.as_bytes()), NONE).is_err(),
            "alias {:?}",
            key
        );
    }
}

#[test]
fn malformed_or_nonnumeric_values_are_refused_without_coercion() {
    for value in [
        "-1",
        "+1",
        "01",
        "00",
        "256",
        "65535",
        "4294967296",
        "18446744073709551616",
        "1.0",
        "1e0",
        "0x1",
        "true",
        "false",
        "null",
        "\"1\"",
        "\"\"",
        "[]",
        "{}",
        "NaN",
        "Infinity",
        "",
    ] {
        let saved = format!("{{\"10\": {}}}", value);
        assert!(
            map(acknowledged(canary()), Some(saved.as_bytes()), NONE).is_err(),
            "value {:?}",
            value
        );
    }
}

#[test]
fn malformed_object_structure_and_trailing_data_are_refused() {
    for saved in [
        "",
        " ",
        "[]",
        "null",
        "10:1",
        "{10:1}",
        "{'10':1}",
        "{{\"10\":1}}",
        "{\"10\":1",
        "\"10\":1}",
        "{\"10\" 1}",
        "{\"10\":1,}",
        "{,\"10\":1}",
        "{\"10\":1,,\"11\":2}",
        "{\"10\":1 \"11\":2}",
        "{\"10\":1:\"11\":2}",
        "{}{}",
        "{} false",
        "{\"10\":1}garbage",
        "{\"10\":1}//comment",
        "/*comment*/{}",
    ] {
        assert_eq!(
            map(acknowledged(canary()), Some(saved.as_bytes()), NONE).is_err(),
            true,
            "syntax {:?}",
            saved
        );
    }
}

#[test]
fn non_json_whitespace_invalid_utf8_bom_and_nul_are_refused() {
    for saved in [
        b"\x0b{}".as_slice(),
        b"{}\x0c".as_slice(),
        b"{\x0b\"10\":1}".as_slice(),
        b"\xef\xbb\xbf{}".as_slice(),
        b"{\"10\":1}\0".as_slice(),
        b"{\"10\":\xff}".as_slice(),
        b"{\"\xff\":1}".as_slice(),
        b"{\"10\":1}\xc2\xa0".as_slice(),
    ] {
        assert!(
            map(acknowledged(canary()), Some(saved), NONE).is_err(),
            "invalid bytes {:?}",
            saved
        );
    }
}

#[test]
fn json_whitespace_is_accepted_at_each_grammar_boundary() {
    let saved = b" \t\r\n{ \t\r\n\"10\" \t\r\n: \t\r\n255 \t\r\n, \t\r\n\"22\" \t\r\n: \t\r\n160 \t\r\n} \t\r\n";
    let frame = canary();
    let mut expected = analog_pairs(&frame);
    expected.push((22, 160));
    assert_eq!(
        mapped(frame, Some(saved), NONE).targets().mixer,
        encode(&expected)
    );
}

#[test]
fn absent_map_and_empty_object_match_but_present_empty_bytes_are_invalid() {
    let frame = canary();
    assert_eq!(mapped(frame, None, NONE), mapped(frame, Some(b"{}"), NONE));
    assert_eq!(
        map(acknowledged(frame), Some(b""), NONE),
        Err(MapError::Syntax)
    );
    assert_eq!(
        map(acknowledged(frame), Some(b" \r\n\t"), NONE),
        Err(MapError::Syntax)
    );
}

#[test]
fn parse_limit_accepts_exact_bound_and_refuses_one_byte_more() {
    assert_eq!(MAX_INPUT_BYTES, 65_536);
    let mut exact = vec![b' '; 65_536];
    exact[0] = b'{';
    exact[1] = b'}';
    assert_eq!(
        mapped(canary(), Some(&exact), NONE),
        mapped(canary(), None, NONE)
    );
    exact.push(b' ');
    assert_eq!(
        map(acknowledged(canary()), Some(&exact), NONE),
        Err(MapError::TooLarge)
    );
    let malformed_oversize = vec![b'x'; 65_537];
    assert_eq!(
        map(acknowledged(canary()), Some(&malformed_oversize), ALL),
        Err(MapError::TooLarge)
    );
}

#[test]
fn saved_bytes_zero_and_255_are_valid_without_modulo_coercion() {
    let frame = canary();
    for byte in [0, 255] {
        let saved = format!("{{\"10\": {}, \"26\": {}, \"27\": {}}}", byte, byte, byte);
        let mut expected = analog_pairs(&frame);
        expected.extend([(26, byte), (27, byte)]);
        assert_eq!(
            mapped(frame, Some(saved.as_bytes()), NONE).targets().mixer,
            encode(&expected)
        );
    }
    assert_eq!(
        map(acknowledged(frame), Some(b"{\"10\": 256}"), NONE),
        Err(MapError::Number)
    );
}

#[test]
fn returned_ack_retains_complete_identity_sequence_request_and_all_152_bytes() {
    for salt in [0, 41] {
        for count in [1, 2, 3] {
            let mut frame = canary();
            frame[151] ^= count as u8;
            let ack = acknowledged_n(frame, count, salt);
            let mapped = map(ack, Some(b"{}"), NONE).unwrap();
            assert_eq!(mapped.ack(), &ack, "complete ACK must be retained");
            assert_eq!(mapped.ack().request.frame(), &frame);
            assert_eq!(mapped.ack().request.request_id(), count);
            let expected = mapped.targets().clone();
            let (retained, targets) = mapped.into_parts();
            assert_eq!(retained, ack);
            assert_eq!(targets, expected);
        }
    }
}

#[test]
fn numeric_key_order_and_serialization_are_deterministic() {
    let frame = [0; 152];
    let first = mapped(
        frame,
        Some(b"{\"80\":255,\"27\":2,\"22\":160,\"26\":1,\"10\":1}"),
        NONE,
    );
    let second = mapped(
        frame,
        Some(b" { \"10\" : 254, \"26\" : 1, \"22\" : 160, \"27\" : 2, \"80\" : 0 } "),
        NONE,
    );
    let mut expected = analog_pairs(&frame);
    expected.extend([(22, 160), (26, 1), (27, 2)]);
    assert_eq!(first.targets().mixer, encode(&expected));
    assert_eq!(first, second);
    assert!(!first.targets().mixer.ends_with(b"\n"));
    assert_eq!(first.targets().knob, b"0");
}

#[test]
fn actual_preview_parser_reads_exact_emitted_supported_values() {
    for selected in [NONE, ALL] {
        let frame = canary();
        let result = mapped(frame, None, selected);
        let text = std::str::from_utf8(&result.targets().mixer).unwrap();
        let parsed = crate::config::parse_byte_map(text);
        let mut expected = analog_pairs(&frame);
        if selected.selector {
            expected.push((22, frame[22] & 0xf0));
        }
        if selected.fx_time {
            expected.extend([(26, frame[26]), (27, frame[27])]);
        }
        expected.sort_unstable_by_key(|&(offset, _)| offset);
        assert_eq!(parsed, expected);
        assert_eq!(result.targets().mixer, encode(&parsed));
    }
}

#[test]
fn actual_preview_mixer_levels_agrees_on_the_51_complete_analog_bytes() {
    for frame in [canary(), [0; 152], [255; 152]] {
        let actual = crate::controls::mixer_levels(&frame);
        assert_eq!(
            actual,
            analog_pairs(&frame),
            "pinned preview control-set interoperability"
        );
        let result = mapped(frame, None, NONE);
        assert_eq!(result.targets().mixer, encode(&actual));
    }
}

#[test]
fn successful_and_refused_calls_leave_input_receipt_and_saved_bytes_unchanged() {
    for bytes in [
        b"{\"22\":160,\"26\":255,\"27\":128}".as_slice(),
        b"{\"10\":1,\"10\":2}".as_slice(),
        b"{\"22\":161}".as_slice(),
        b"{\"26\":7}".as_slice(),
        b"not json".as_slice(),
    ] {
        let saved = bytes.to_vec();
        let original = saved.clone();
        let ack = acknowledged(canary());
        let before = ack;
        let _ = map(ack, Some(&saved), ALL);
        assert_eq!(saved, original);
        assert_eq!(ack, before);
        assert_eq!(ack.request.frame(), &canary());
    }
}

#[test]
fn mapping_is_repeatable_and_cloned_results_keep_owned_exact_values() {
    let bytes = b"{\"22\":160,\"26\":255,\"27\":128}".to_vec();
    let ack = acknowledged(canary());
    let first = map(ack, Some(&bytes), ALL).unwrap();
    let copied = first.clone();
    let repeated = map(ack, Some(&bytes), ALL).unwrap();
    assert_eq!(first, copied);
    assert_eq!(first, repeated);
    let expected = copied.targets().clone();
    let (_, mut taken) = first.into_parts();
    taken.knob.clear();
    taken.mixer.clear();
    assert_eq!(
        copied.targets(),
        &expected,
        "result owns its exact byte strings"
    );
}
