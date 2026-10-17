/* Official SDK harness has no frame_filter service. Verify graceful degradation;
 * test_shortcut.c and test_component.c exercise the real editable mixer path. */
static int harness_scenarios(const char *id)
{
    if (strcmp(id, "mods-shortcut")) return 0;
    D.view = AZ_VIEW_DECK; D.count = 4; ticks(1);
    expect(nsurf == 1, "one SDK surface only");
    expect(surfs[0].r.x == 1080 && surfs[0].r.y == 50, "four-deck header position");
    expect(!surfs[0].shown, "missing frame_filter hides inert shortcut");
    D.count = 2; ticks(1);
    expect(surfs[0].r.x == 900 && surfs[0].r.y == 50, "two-deck header position");
    expect(!surfs[0].shown, "missing input service stays hidden");
    return 1;
}
