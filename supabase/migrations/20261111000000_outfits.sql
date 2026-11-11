-- Saved outfits of Digital Closet. ADDITIVE on top of 20261110120000_garments.sql: only creates
-- new tables and functions; it does not alter or remove anything that already exists.
--
-- Recommendation keeps its own proposals (Outfit_recommendations); an outfit the user decides to
-- keep is copied here, so it survives Recommendation purging its data. recommendation_id is a
-- logical reference to that other database (no foreign key), only for traceability: it grants
-- nothing. Same security model as garments: RLS on, no policies, only service_role calls the
-- functions, and every function takes the verified owner (p_user_id).
begin;

create table public.outfits (
    id uuid primary key,
    wardrobe_id uuid not null references public.wardrobes(id),
    name text check (name is null or length(btrim(name)) between 1 and 120),
    occasion varchar(50),
    style varchar(50),
    source varchar(20) not null default 'MANUAL' check (source in ('MANUAL','RECOMMENDATION')),
    recommendation_id uuid,
    created_at timestamptz not null default now(),
    -- Soft delete, like garments.
    deleted_at timestamptz,
    check ((source = 'RECOMMENDATION') = (recommendation_id is not null))
);
create index outfits_wardrobe_id_id on public.outfits (wardrobe_id, id);

-- Same shape as Recommendation_garments in the design diagram, plus the display order.
create table public.outfit_garments (
    outfit_id uuid not null references public.outfits(id),
    garment_id uuid not null references public.garments(id),
    position smallint not null check (position between 1 and 3),
    primary key (outfit_id, garment_id),
    unique (outfit_id, position)
);

alter table public.outfits enable row level security;
alter table public.outfit_garments enable row level security;
revoke all on public.outfits, public.outfit_garments from anon,authenticated;

-- `complete` is false when a garment of the outfit was removed from the wardrobe afterwards.
create function public.mirage_outfit_json(o public.outfits) returns jsonb
language sql stable set search_path = '' as $$
    select jsonb_build_object(
        'id', o.id, 'name', o.name, 'occasion', o.occasion, 'style', o.style,
        'source', o.source, 'recommendation_id', o.recommendation_id,
        'garment_ids', coalesce((select jsonb_agg(og.garment_id order by og.position)
                                 from public.outfit_garments og where og.outfit_id = o.id), '[]'::jsonb),
        'complete', not exists (select 1 from public.outfit_garments og
                                join public.garments g on g.id = og.garment_id
                                where og.outfit_id = o.id and g.deleted_at is not null),
        'created_at', to_char(o.created_at at time zone 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"'));
$$;

-- Saves an outfit of 1 to 3 distinct garments. Garments of other owners, deleted or unknown
-- are indistinguishable (no_data_found); unavailable ones are rejected (check_violation).
create function public.mirage_create_outfit(p_user_id uuid, p_outfit jsonb) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare
    ids uuid[];
    wardrobe uuid;
    created public.outfits;
begin
    perform mirage_private.assert_user(p_user_id);
    ids := array(select e::uuid from jsonb_array_elements_text(p_outfit->'garment_ids') with ordinality t(e, o) order by o);
    if cardinality(ids) not between 1 and 3
       or (select count(distinct i) from unnest(ids) i) <> cardinality(ids) then
        raise check_violation;
    end if;
    wardrobe := mirage_private.wardrobe_of(p_user_id);
    if (select count(*) from public.garments g
        where g.id = any(ids) and g.deleted_at is null and g.wardrobe_id = wardrobe) <> cardinality(ids) then
        raise no_data_found;
    end if;
    if exists (select 1 from public.garments g where g.id = any(ids) and g.availability <> 'AVAILABLE') then
        raise check_violation;
    end if;
    insert into public.outfits(id, wardrobe_id, name, occasion, style, source, recommendation_id)
    values ((p_outfit->>'id')::uuid, wardrobe, p_outfit->>'name', p_outfit->>'occasion', p_outfit->>'style',
            case when p_outfit->>'recommendation_id' is null then 'MANUAL' else 'RECOMMENDATION' end,
            (p_outfit->>'recommendation_id')::uuid)
    returning * into created;
    insert into public.outfit_garments(outfit_id, garment_id, position)
        select created.id, i, o::smallint from unnest(ids) with ordinality t(i, o);
    return public.mirage_outfit_json(created);
end;
$$;

-- shortcut: ordered by id (like garments) with the last id as cursor, not newest first;
-- switch to a (created_at, id) cursor if the app needs recency order.
create function public.mirage_list_outfits(p_user_id uuid, p_limit integer, p_cursor uuid) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare ids uuid[];
begin
    perform mirage_private.assert_user(p_user_id);
    if p_limit is null or p_limit not between 1 and 100 then raise check_violation; end if;
    ids := array(select o.id from public.outfits o
                 where o.wardrobe_id = mirage_private.wardrobe_of(p_user_id) and o.deleted_at is null
                   and (p_cursor is null or o.id > p_cursor)
                 order by o.id limit p_limit + 1);
    return jsonb_build_object(
        'items', coalesce((select jsonb_agg(public.mirage_outfit_json(o) order by o.id)
                           from public.outfits o where o.id = any(ids[1:p_limit])), '[]'::jsonb),
        'next_cursor', case when cardinality(ids) > p_limit then to_jsonb(ids[p_limit]) else null end);
end;
$$;

-- Returns null when the outfit does not exist or belongs to someone else.
create function public.mirage_get_outfit(p_user_id uuid, p_id uuid) returns jsonb
language plpgsql security invoker set search_path = '' as $$
begin
    perform mirage_private.assert_user(p_user_id);
    return (select public.mirage_outfit_json(o) from public.outfits o
            where o.id = p_id and o.deleted_at is null and o.wardrobe_id = mirage_private.wardrobe_of(p_user_id));
end;
$$;

create function public.mirage_remove_outfit(p_user_id uuid, p_id uuid) returns jsonb
language plpgsql security invoker set search_path = '' as $$
begin
    perform mirage_private.assert_user(p_user_id);
    update public.outfits set deleted_at = now()
        where id = p_id and deleted_at is null and wardrobe_id = mirage_private.wardrobe_of(p_user_id);
    if not found then raise no_data_found; end if;
    return jsonb_build_object('id', p_id);
end;
$$;

revoke all on function public.mirage_outfit_json(public.outfits), public.mirage_create_outfit(uuid,jsonb),
    public.mirage_list_outfits(uuid,integer,uuid), public.mirage_get_outfit(uuid,uuid),
    public.mirage_remove_outfit(uuid,uuid) from public,anon,authenticated;
grant execute on function public.mirage_outfit_json(public.outfits), public.mirage_create_outfit(uuid,jsonb),
    public.mirage_list_outfits(uuid,integer,uuid), public.mirage_get_outfit(uuid,uuid),
    public.mirage_remove_outfit(uuid,uuid) to service_role;
commit;
