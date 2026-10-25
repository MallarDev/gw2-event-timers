//! Strict decoding of GW2's five-byte map chat links.
pub fn waypoint_id(link: &str) -> Option<u32> {
    let payload = link.strip_prefix("[&")?.strip_suffix(']')?.as_bytes();
    if payload.len() != 8 || payload[7] != b'=' { return None; }
    let mut bits = 0u64;
    for &byte in &payload[..7] {
        let value = match byte {
            b'A'..=b'Z' => byte - b'A',
            b'a'..=b'z' => byte - b'a' + 26,
            b'0'..=b'9' => byte - b'0' + 52,
            b'+' => 62,
            b'/' => 63,
            _ => return None,
        };
        bits = (bits << 6) | u64::from(value);
    }
    if bits & 3 != 0 { return None; }
    let bytes = (bits >> 2).to_be_bytes();
    if bytes[3] != 4 { return None; }
    let id = u32::from_le_bytes(bytes[4..8].try_into().ok()?);
    (id != 0).then_some(id)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn decodes_the_game_validated_destinations() {
        assert_eq!(waypoint_id("[&BDAEAAA=]"), Some(1072));
        assert_eq!(waypoint_id("[&BA4EAAA=]"), Some(1038));
    }
    #[test]
    fn rejects_non_map_links_bad_padding_and_extra_content() {
        for invalid in ["", "[&AgAAAAA=]", "[&BAAAAAA=]", "[&BDAEAAA=] extra", "[&BDAEAAB=]", "[&BDAEAAA]", "[&BDAEAA\0=]", "[&BDAEAAA==]", "[&BDAEAAé=]"] {
            assert_eq!(waypoint_id(invalid), None, "{invalid:?}");
        }
    }
}
