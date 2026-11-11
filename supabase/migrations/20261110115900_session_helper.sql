-- Prerequisite copied from iam-service (20261109153938_active_sessions.sql), without its
-- user_profiles policy change. Needed only when this project is not the IAM project; if
-- IAM already installed these objects, skip this file.
begin;
create schema mirage_private;
revoke all on schema mirage_private from public,anon;
grant usage on schema mirage_private to authenticated;

create function mirage_private.session_active() returns boolean
language sql stable security definer set search_path = '' as $$
    select auth.uid() is not null and exists (
        select 1 from auth.sessions s
        where s.id = (auth.jwt()->>'session_id')::uuid
          and s.user_id = auth.uid()
          and (s.not_after is null or s.not_after > now())
    );
$$;
revoke all on function mirage_private.session_active() from public,anon;
grant execute on function mirage_private.session_active() to authenticated;

create function public.mirage_session_active() returns boolean
language sql stable security invoker set search_path = '' as $$
    select mirage_private.session_active();
$$;
revoke all on function public.mirage_session_active() from public,anon;
grant execute on function public.mirage_session_active() to authenticated;
commit;
