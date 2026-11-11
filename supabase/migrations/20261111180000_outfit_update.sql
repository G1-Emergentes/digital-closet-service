-- Editing a saved outfit: name, occasion, style and its pieces. ADDITIVE on top of the previous
-- outfit migrations: it only adds one function; no table or existing function is touched.
--
-- Like PUT /garments/{id}, the request carries the whole outfit and replaces it. Adding a piece
-- to an outfit is "the current pieces + the new one". The pieces of that one outfit are
-- rewritten when they change; the look picture is cleared in that case because it no longer
-- shows this combination (the stored file is left in the bucket, unreferenced).
begin;

-- Errors: unknown outfit -> no_data_found (P0002); a missing/foreign garment -> MG003;
-- a bad count/duplicates, or a NEW piece that is unavailable -> check_violation.
create function public.mirage_update_outfit(p_user_id uuid, p_id uuid, p_outfit jsonb) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare
    existing public.outfits;
    changed public.outfits;
    ids uuid[];
    wardrobe uuid;
    current_ids uuid[];
    same_pieces boolean;
begin
    perform mirage_private.assert_user(p_user_id);
    wardrobe := mirage_private.wardrobe_of(p_user_id);
    select * into existing from public.outfits
        where id = p_id and deleted_at is null and wardrobe_id = wardrobe for update;
    if not found then raise no_data_found; end if;

    ids := array(select e::uuid from jsonb_array_elements_text(p_outfit->'garment_ids') with ordinality t(e, o) order by o);
    if cardinality(ids) not between 1 and 3
       or (select count(distinct i) from unnest(ids) i) <> cardinality(ids) then
        raise check_violation;
    end if;
    if (select count(*) from public.garments g
        where g.id = any(ids) and g.deleted_at is null and g.wardrobe_id = wardrobe) <> cardinality(ids) then
        raise exception 'garment_not_found' using errcode = 'MG003';
    end if;
    current_ids := array(select og.garment_id from public.outfit_garments og
                         where og.outfit_id = p_id order by og.position);
    -- Pieces already in the outfit may have become unavailable since; only new ones must be available.
    if exists (select 1 from public.garments g where g.id = any(ids) and g.availability <> 'AVAILABLE'
               and not (g.id = any(current_ids))) then
        raise check_violation;
    end if;

    same_pieces := array(select i from unnest(ids) i order by i) = array(select i from unnest(current_ids) i order by i);
    if ids <> current_ids then
        delete from public.outfit_garments where outfit_id = p_id;
        insert into public.outfit_garments(outfit_id, garment_id, position)
            select p_id, i, o::smallint from unnest(ids) with ordinality t(i, o);
    end if;

    update public.outfits set
        name = p_outfit->>'name', occasion = p_outfit->>'occasion', style = p_outfit->>'style',
        image_id = case when same_pieces then image_id end,
        image_url = case when same_pieces then image_url end,
        image_version = case when same_pieces then image_version end
    where id = p_id returning * into changed;
    return public.mirage_outfit_json(changed);
end;
$$;

revoke all on function public.mirage_update_outfit(uuid,uuid,jsonb) from public,anon,authenticated;
grant execute on function public.mirage_update_outfit(uuid,uuid,jsonb) to service_role;
commit;
