-- Look image of a saved outfit. ADDITIVE on top of 20261111000000_outfits.sql: adds nullable
-- columns, replaces one read function with a superset and adds two functions. Nothing is
-- dropped, and existing outfits keep working (they simply have no image).
--
-- The image lives in the existing private bucket garment-images under
-- {user_id}/outfits/{outfit_id}/{image_id} (four segments, so it never collides with
-- garment photos at {user_id}/{garment_id}/{image_id}). The service signs uploads/downloads.
begin;

alter table public.outfits
    add column image_id uuid,
    add column image_url varchar(255),
    add column image_version integer check (image_version > 0),
    add constraint outfits_image_all_or_none check (
        (image_id is null) = (image_version is null) and (image_id is null) = (image_url is null));

-- Same JSON as before plus `image` ({id, version} or null).
create or replace function public.mirage_outfit_json(o public.outfits) returns jsonb
language sql stable set search_path = '' as $$
    select jsonb_build_object(
        'id', o.id, 'name', o.name, 'occasion', o.occasion, 'style', o.style,
        'source', o.source, 'recommendation_id', o.recommendation_id,
        'garment_ids', coalesce((select jsonb_agg(og.garment_id order by og.position)
                                 from public.outfit_garments og where og.outfit_id = o.id), '[]'::jsonb),
        'complete', not exists (select 1 from public.outfit_garments og
                                join public.garments g on g.id = og.garment_id
                                where og.outfit_id = o.id and g.deleted_at is not null),
        'image', case when o.image_id is null then null
                      else jsonb_build_object('id', o.image_id, 'version', o.image_version) end,
        'created_at', to_char(o.created_at at time zone 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"'));
$$;

-- Registers an image the client already uploaded. A new id starts at version 1; the same id
-- again means its content was replaced, so the version grows.
create function public.mirage_set_outfit_image(p_user_id uuid, p_id uuid, p_image_id uuid) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare
    existing public.outfits;
    changed public.outfits;
    object_path text := p_user_id::text || '/outfits/' || p_id::text || '/' || p_image_id::text;
begin
    perform mirage_private.assert_user(p_user_id);
    select * into existing from public.outfits
        where id = p_id and deleted_at is null and wardrobe_id = mirage_private.wardrobe_of(p_user_id) for update;
    if not found then raise no_data_found; end if;
    if not exists (select 1 from storage.objects o where o.bucket_id = 'garment-images' and o.name = object_path) then
        raise exception 'image_not_uploaded' using errcode = 'MG001';
    end if;
    update public.outfits set image_id = p_image_id, image_url = object_path,
        image_version = case when existing.image_id is not distinct from p_image_id
                             then existing.image_version + 1 else 1 end
    where id = p_id returning * into changed;
    return public.mirage_outfit_json(changed);
end;
$$;

-- Where the look lives in the bucket; the service signs it for five minutes.
create function public.mirage_outfit_image(p_user_id uuid, p_id uuid) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare o public.outfits;
begin
    perform mirage_private.assert_user(p_user_id);
    select * into o from public.outfits
        where id = p_id and deleted_at is null and wardrobe_id = mirage_private.wardrobe_of(p_user_id);
    if not found then raise no_data_found; end if;
    if o.image_id is null then
        return jsonb_build_object('image_id', null);
    end if;
    return jsonb_build_object('image_id', o.image_id, 'image_version', o.image_version,
        'path', o.image_url,
        'mime_type', coalesce((select s.metadata->>'mimetype' from storage.objects s
            where s.bucket_id = 'garment-images' and s.name = o.image_url), 'application/octet-stream'));
end;
$$;

revoke all on function public.mirage_set_outfit_image(uuid,uuid,uuid), public.mirage_outfit_image(uuid,uuid)
    from public,anon,authenticated;
grant execute on function public.mirage_set_outfit_image(uuid,uuid,uuid), public.mirage_outfit_image(uuid,uuid)
    to service_role;
commit;
