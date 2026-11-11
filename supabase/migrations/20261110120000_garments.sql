-- Digital Closet (Wardrobe Database), following the project's design diagram:
-- wardrobes, garment_categories, garment_types, garments, garment_secondary_colors,
-- store_garment_references. The contract's extra fields (styles, description, availability,
-- revision, versioned image, soft delete) are extra columns on garments.
-- Additive: aborts on conflicting object names, never drops existing data.
-- Requires public.mirage_session_active and schema mirage_private (IAM, or session_helper).
begin;

-- Used by CHECK constraints: true when an array has no repeated items.
create function mirage_private.is_set(items text[]) returns boolean
language sql immutable set search_path = '' as $$
    select cardinality(items) = (select count(distinct item) from unnest(items) item);
$$;
revoke all on function mirage_private.is_set(text[]) from public,anon;
grant execute on function mirage_private.is_set(text[]) to authenticated;

-- One wardrobe per user, created with the first garment.
create table public.wardrobes (
    id uuid primary key default gen_random_uuid(),
    user_id uuid not null unique references auth.users(id),
    created_at timestamptz not null default now()
);
create table public.garment_categories (
    id uuid primary key default gen_random_uuid(),
    name varchar(50) not null unique
);
-- Types are the contract's open `subcategory` codes. The type named like its category is
-- the "no subcategory" type, so garments.garment_type_id stays mandatory as in the diagram.
create table public.garment_types (
    id uuid primary key default gen_random_uuid(),
    category_id uuid not null references public.garment_categories(id),
    name varchar(60) not null unique check (name ~ '^[A-Z0-9_]{1,60}$')
);
insert into public.garment_categories(name)
values ('TOP'),('BOTTOM'),('ONE_PIECE'),('OUTERWEAR'),('FOOTWEAR'),('ACCESSORY');
insert into public.garment_types(category_id,name) select id,name from public.garment_categories;

create table public.garments (
    id uuid primary key,
    wardrobe_id uuid not null references public.wardrobes(id),
    garment_type_id uuid not null references public.garment_types(id),
    name text not null check (length(btrim(name)) between 1 and 120),
    -- Nullable (diagram: mandatory): the contract allows garments with unknown colours.
    primary_color varchar(30) check (primary_color in ('BLACK','WHITE','GRAY','BLUE','RED','GREEN','YELLOW','ORANGE','PURPLE','PINK','BROWN','BEIGE','MULTICOLOR')),
    pattern varchar(30),
    -- Object path inside the private bucket, not a public URL.
    image_url varchar(255),
    favorite boolean not null default false,
    source varchar(20) not null default 'MANUAL' check (length(btrim(source)) between 1 and 20),
    created_at timestamptz not null default now(),
    updated_at timestamptz not null default now(),
    -- Contract fields beyond the diagram.
    styles text[] not null default '{}' check (
        styles <@ array['CASUAL','FORMAL','SPORT','STREETWEAR','MINIMALIST','CLASSIC','BOHEMIAN']
        and mirage_private.is_set(styles)),
    description text not null check (length(btrim(description)) between 1 and 1000),
    availability text not null default 'AVAILABLE' check (availability in ('AVAILABLE','UNAVAILABLE')),
    image_id uuid,
    image_version integer check (image_version > 0),
    revision integer not null default 1 check (revision > 0),
    -- Soft delete: a deleted garment resolves as NOT_FOUND and leaves the listing.
    deleted_at timestamptz,
    check ((image_id is null) = (image_version is null) and (image_id is null) = (image_url is null))
);
create index garments_wardrobe_id_id on public.garments (wardrobe_id, id);

create table public.garment_secondary_colors (
    garment_id uuid not null references public.garments(id),
    color varchar(30) not null check (color in ('BLACK','WHITE','GRAY','BLUE','RED','GREEN','YELLOW','ORANGE','PURPLE','PINK','BROWN','BEIGE','MULTICOLOR')),
    primary key (garment_id, color)
);
-- Link to the Store Catalog bounded context; catalog_item_id has no FK (another database).
create table public.store_garment_references (
    garment_id uuid primary key references public.garments(id),
    catalog_item_id uuid
);

-- Every update to garments is an observable change: revision always grows. The RPCs skip
-- the write when nothing changed. Identity columns are immutable.
create function mirage_private.garment_revision() returns trigger
language plpgsql set search_path = '' as $$
begin
    new.id := old.id;
    new.wardrobe_id := old.wardrobe_id;
    new.created_at := old.created_at;
    new.revision := old.revision + 1;
    new.updated_at := now();
    return new;
end;
$$;
create trigger garment_revision before update on public.garments
    for each row execute function mirage_private.garment_revision();

alter table public.wardrobes enable row level security;
alter table public.garment_categories enable row level security;
alter table public.garment_types enable row level security;
alter table public.garments enable row level security;
alter table public.garment_secondary_colors enable row level security;
alter table public.store_garment_references enable row level security;

create policy wardrobe_owner on public.wardrobes for all to authenticated
    using ((select auth.uid()) = user_id and (select public.mirage_session_active()))
    with check ((select auth.uid()) = user_id and (select public.mirage_session_active()));
create policy category_read on public.garment_categories for select to authenticated using (true);
create policy type_read on public.garment_types for select to authenticated using (true);
-- shortcut: any signed-in user can add a type code; curate the list if it gets abused.
create policy type_create on public.garment_types for insert to authenticated
    with check (name ~ '^[A-Z0-9_]{1,60}$' and (select public.mirage_session_active()));
create policy garment_owner on public.garments for all to authenticated
    using (exists (select 1 from public.wardrobes w where w.id = wardrobe_id
                   and w.user_id = (select auth.uid())) and (select public.mirage_session_active()))
    with check (exists (select 1 from public.wardrobes w where w.id = wardrobe_id
                        and w.user_id = (select auth.uid())) and (select public.mirage_session_active()));
create policy secondary_color_owner on public.garment_secondary_colors for all to authenticated
    using (exists (select 1 from public.garments g join public.wardrobes w on w.id = g.wardrobe_id
                   where g.id = garment_id and w.user_id = (select auth.uid()))
           and (select public.mirage_session_active()))
    with check (exists (select 1 from public.garments g join public.wardrobes w on w.id = g.wardrobe_id
                        where g.id = garment_id and w.user_id = (select auth.uid()))
                and (select public.mirage_session_active()));
create policy store_reference_owner on public.store_garment_references for all to authenticated
    using (exists (select 1 from public.garments g join public.wardrobes w on w.id = g.wardrobe_id
                   where g.id = garment_id and w.user_id = (select auth.uid()))
           and (select public.mirage_session_active()))
    with check (exists (select 1 from public.garments g join public.wardrobes w on w.id = g.wardrobe_id
                        where g.id = garment_id and w.user_id = (select auth.uid()))
                and (select public.mirage_session_active()));
revoke all on public.wardrobes, public.garment_categories, public.garment_types, public.garments,
    public.garment_secondary_colors, public.store_garment_references from anon,authenticated;
grant select,insert on public.wardrobes to authenticated;
grant select on public.garment_categories to authenticated;
grant select,insert on public.garment_types to authenticated;
grant select,insert,update on public.garments to authenticated;
grant select,insert,delete on public.garment_secondary_colors, public.store_garment_references to authenticated;

-- Private bucket. Clients upload to {user_id}/{garment_id}/{image_id}; only their own folder.
insert into storage.buckets(id,name,public,file_size_limit,allowed_mime_types)
values ('garment-images','garment-images',false,8388608,array['image/jpeg','image/png','image/webp'])
on conflict (id) do nothing;
create policy garment_image_read on storage.objects for select to authenticated
    using (bucket_id='garment-images' and (storage.foldername(name))[1]=(select auth.uid())::text and (select public.mirage_session_active()));
create policy garment_image_insert on storage.objects for insert to authenticated
    with check (bucket_id='garment-images' and (storage.foldername(name))[1]=(select auth.uid())::text and (select public.mirage_session_active()));
create policy garment_image_update on storage.objects for update to authenticated
    using (bucket_id='garment-images' and (storage.foldername(name))[1]=(select auth.uid())::text and (select public.mirage_session_active()))
    with check (bucket_id='garment-images' and (storage.foldername(name))[1]=(select auth.uid())::text and (select public.mirage_session_active()));
create policy garment_image_delete on storage.objects for delete to authenticated
    using (bucket_id='garment-images' and (storage.foldername(name))[1]=(select auth.uid())::text and (select public.mirage_session_active()));

-- A rejected or revoked session fails loudly (42501) instead of looking like an empty closet.
create function mirage_private.assert_session() returns void
language plpgsql set search_path = '' as $$
begin
    if auth.uid() is null or not public.mirage_session_active() then
        raise insufficient_privilege;
    end if;
end;
$$;
revoke all on function mirage_private.assert_session() from public,anon;
grant execute on function mirage_private.assert_session() to authenticated;

-- Contract `colors`: the primary colour first, then the secondary ones sorted.
create function mirage_private.garment_colors(g public.garments) returns text[]
language sql stable set search_path = '' as $$
    select case when g.primary_color is null then '{}'::text[]
        else array[g.primary_color::text] || coalesce(array(
            select c.color::text from public.garment_secondary_colors c
            where c.garment_id = g.id order by c.color), '{}'::text[]) end;
$$;
revoke all on function mirage_private.garment_colors(public.garments) from public,anon;
grant execute on function mirage_private.garment_colors(public.garments) to authenticated;

-- Same shape as the contract's colour list for a request: first = primary, rest sorted.
create function mirage_private.canonical_colors(cols text[]) returns text[]
language sql immutable set search_path = '' as $$
    select case when cardinality(cols) = 0 then '{}'::text[]
        else array[cols[1]] || coalesce(array(select c from unnest(cols[2:]) c order by c), '{}'::text[]) end;
$$;
revoke all on function mirage_private.canonical_colors(text[]) from public,anon;
grant execute on function mirage_private.canonical_colors(text[]) to authenticated;

-- Finds the type for a category/subcategory pair, creating a new subcategory on demand.
create function mirage_private.garment_type_id(p_category text, p_subcategory text) returns uuid
language plpgsql set search_path = '' as $$
declare
    type_name text := coalesce(p_subcategory, p_category);
    found_id uuid;
    found_category text;
begin
    -- Validate before inserting: a bad code is a 400 (check_violation), not an RLS denial.
    if type_name is null or type_name !~ '^[A-Z0-9_]{1,60}$' then raise check_violation; end if;
    insert into public.garment_types(category_id, name)
        select c.id, type_name from public.garment_categories c where c.name = p_category
    on conflict (name) do nothing;
    select t.id, c.name into found_id, found_category
        from public.garment_types t join public.garment_categories c on c.id = t.category_id
        where t.name = type_name;
    -- Unknown category, or a subcategory that already belongs to another category.
    if found_id is null or found_category <> p_category then raise check_violation; end if;
    return found_id;
end;
$$;
revoke all on function mirage_private.garment_type_id(text,text) from public,anon;
grant execute on function mirage_private.garment_type_id(text,text) to authenticated;

-- GarmentReadModel of the contract (snake_case).
create function public.mirage_garment_json(g public.garments) returns jsonb
language sql stable set search_path = '' as $$
    select jsonb_build_object(
        'id', g.id, 'name', g.name, 'category', c.name,
        'subcategory', case when t.name = c.name then null else t.name end,
        'colors', to_jsonb(mirage_private.garment_colors(g)), 'styles', to_jsonb(g.styles),
        'description', g.description, 'availability', g.availability,
        'image', case when g.image_id is null then null
                      else jsonb_build_object('id', g.image_id, 'version', g.image_version) end,
        'revision', g.revision,
        'updated_at', to_char(g.updated_at at time zone 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"'))
    from public.garment_types t join public.garment_categories c on c.id = t.category_id
    where t.id = g.garment_type_id;
$$;

create function public.mirage_create_garment(p_garment jsonb) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare
    wardrobe uuid;
    cols text[];
    created public.garments;
begin
    perform mirage_private.assert_session();
    cols := array(select e from jsonb_array_elements_text(p_garment->'colors') with ordinality t(e, o) order by o);
    if cardinality(cols) <> (select count(distinct x) from unnest(cols) x) then raise check_violation; end if;
    insert into public.wardrobes(user_id) values (auth.uid()) on conflict (user_id) do nothing;
    select id into wardrobe from public.wardrobes where user_id = auth.uid();
    insert into public.garments(id,wardrobe_id,garment_type_id,name,primary_color,styles,description,availability)
    values ((p_garment->>'id')::uuid, wardrobe,
        mirage_private.garment_type_id(p_garment->>'category', p_garment->>'subcategory'),
        p_garment->>'name', cols[1],
        coalesce(array(select e from jsonb_array_elements_text(p_garment->'styles') with ordinality t(e, o) order by o), '{}'),
        p_garment->>'description', coalesce(p_garment->>'availability', 'AVAILABLE'))
    returning * into created;
    insert into public.garment_secondary_colors(garment_id, color) select created.id, c from unnest(cols[2:]) c;
    return public.mirage_garment_json(created);
end;
$$;

-- Replaces every attribute. A request that changes nothing leaves revision untouched.
create function public.mirage_update_garment(p_id uuid, p_garment jsonb) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare
    existing public.garments;
    changed public.garments;
    cols text[];
    new_styles text[];
    new_type uuid;
begin
    perform mirage_private.assert_session();
    select * into existing from public.garments where id = p_id and deleted_at is null for update;
    if not found then raise no_data_found; end if;
    cols := array(select e from jsonb_array_elements_text(p_garment->'colors') with ordinality t(e, o) order by o);
    if cardinality(cols) <> (select count(distinct x) from unnest(cols) x) then raise check_violation; end if;
    new_styles := coalesce(array(select e from jsonb_array_elements_text(p_garment->'styles') with ordinality t(e, o) order by o), '{}');
    new_type := mirage_private.garment_type_id(p_garment->>'category', p_garment->>'subcategory');
    if existing.garment_type_id = new_type and existing.name = p_garment->>'name'
       and existing.description = p_garment->>'description'
       and existing.availability = p_garment->>'availability' and existing.styles = new_styles
       and mirage_private.garment_colors(existing) = mirage_private.canonical_colors(cols) then
        return public.mirage_garment_json(existing);
    end if;
    delete from public.garment_secondary_colors where garment_id = p_id;
    update public.garments set garment_type_id = new_type, name = p_garment->>'name',
        primary_color = cols[1], styles = new_styles, description = p_garment->>'description',
        availability = p_garment->>'availability'
    where id = p_id returning * into changed;
    insert into public.garment_secondary_colors(garment_id, color) select p_id, c from unnest(cols[2:]) c;
    return public.mirage_garment_json(changed);
end;
$$;

create function public.mirage_delete_garment(p_id uuid) returns jsonb
language plpgsql security invoker set search_path = '' as $$
begin
    perform mirage_private.assert_session();
    update public.garments set deleted_at = now() where id = p_id and deleted_at is null;
    if not found then raise no_data_found; end if;
    return jsonb_build_object('id', p_id);
end;
$$;

create function public.mirage_get_garment(p_id uuid) returns jsonb
language plpgsql security invoker set search_path = '' as $$
begin
    perform mirage_private.assert_session();
    return (select public.mirage_garment_json(g) from public.garments g
            where g.id = p_id and g.deleted_at is null);
end;
$$;

-- Stable order by id; the cursor is the last id of the previous page.
create function public.mirage_list_garments(p_limit integer, p_cursor uuid) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare ids uuid[];
begin
    perform mirage_private.assert_session();
    if p_limit is null or p_limit not between 1 and 100 then raise check_violation; end if;
    ids := array(select g.id from public.garments g
                 where g.deleted_at is null and (p_cursor is null or g.id > p_cursor)
                 order by g.id limit p_limit + 1);
    return jsonb_build_object(
        'items', coalesce((select jsonb_agg(public.mirage_garment_json(g) order by g.id)
                           from public.garments g where g.id = any(ids[1:p_limit])), '[]'::jsonb),
        'next_cursor', case when cardinality(ids) > p_limit then to_jsonb(ids[p_limit]) else null end);
end;
$$;

-- One result per requested id, in request order. Other owners' garments are NOT_FOUND.
create function public.mirage_resolve_garments(p_ids uuid[]) returns jsonb
language plpgsql security invoker set search_path = '' as $$
begin
    perform mirage_private.assert_session();
    if p_ids is null or cardinality(p_ids) not between 1 and 100
       or (select count(distinct i) from unnest(p_ids) i) <> cardinality(p_ids) then
        raise check_violation;
    end if;
    return jsonb_build_object('results', (
        select jsonb_agg(jsonb_build_object(
            'id', r.id,
            'status', case when g.id is null then 'NOT_FOUND'
                           when g.availability = 'AVAILABLE' then 'AVAILABLE' else 'UNAVAILABLE' end,
            'garment', case when g.availability = 'AVAILABLE' then public.mirage_garment_json(g) end)
            order by r.ord)
        from unnest(p_ids) with ordinality r(id, ord)
        left join public.garments g on g.id = r.id and g.deleted_at is null));
end;
$$;

-- Registers an image the client already uploaded. A new id starts at version 1; the same
-- id again means its content was replaced, so the version grows.
create function public.mirage_set_garment_image(p_id uuid, p_image_id uuid) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare
    existing public.garments;
    changed public.garments;
    object_path text := auth.uid()::text || '/' || p_id::text || '/' || p_image_id::text;
begin
    perform mirage_private.assert_session();
    select * into existing from public.garments where id = p_id and deleted_at is null for update;
    if not found then raise no_data_found; end if;
    if not exists (select 1 from storage.objects o where o.bucket_id = 'garment-images' and o.name = object_path) then
        raise exception 'image_not_uploaded' using errcode = 'MG001';
    end if;
    update public.garments set image_id = p_image_id, image_url = object_path,
        image_version = case when existing.image_id is not distinct from p_image_id
                             then existing.image_version + 1 else 1 end
    where id = p_id returning * into changed;
    return public.mirage_garment_json(changed);
end;
$$;

-- Where the photo lives in the bucket; the service signs it for five minutes.
create function public.mirage_garment_image(p_id uuid) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare g public.garments;
begin
    perform mirage_private.assert_session();
    select * into g from public.garments where id = p_id and deleted_at is null;
    if not found then raise no_data_found; end if;
    if g.image_id is null then
        return jsonb_build_object('image_id', null);
    end if;
    return jsonb_build_object('image_id', g.image_id, 'image_version', g.image_version,
        'path', g.image_url,
        'mime_type', coalesce((select o.metadata->>'mimetype' from storage.objects o
            where o.bucket_id = 'garment-images' and o.name = g.image_url), 'application/octet-stream'));
end;
$$;

revoke all on function public.mirage_garment_json(public.garments),
    public.mirage_create_garment(jsonb), public.mirage_update_garment(uuid,jsonb),
    public.mirage_delete_garment(uuid), public.mirage_get_garment(uuid),
    public.mirage_list_garments(integer,uuid), public.mirage_resolve_garments(uuid[]),
    public.mirage_set_garment_image(uuid,uuid), public.mirage_garment_image(uuid)
    from public,anon;
grant execute on function public.mirage_garment_json(public.garments),
    public.mirage_create_garment(jsonb), public.mirage_update_garment(uuid,jsonb),
    public.mirage_delete_garment(uuid), public.mirage_get_garment(uuid),
    public.mirage_list_garments(integer,uuid), public.mirage_resolve_garments(uuid[]),
    public.mirage_set_garment_image(uuid,uuid), public.mirage_garment_image(uuid)
    to authenticated;
commit;
